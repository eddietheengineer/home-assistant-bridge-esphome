# Code Review: home-assistant-bridge-esphome

**Date:** 2026-06-22
**Reviewer:** AI Code Review
**Scope:** Full module-by-module review of the `geappliances_bridge` ESPHome component

---

## Executive Summary

The codebase is well-architected with clear module boundaries, proper separation of concerns, and comprehensive test coverage (347 tests, all passing). The state machine design (startup HSM, polling HSM, subscription HSM, write HSM) is well-thought-out. However, there are several issues ranging from critical memory safety concerns to architectural inconsistencies.

**Severity Legend:**
- 🔴 **CRITICAL** — Can cause crashes, data corruption, or security issues
- 🟠 **HIGH** — Can cause intermittent failures or significant resource issues
- 🟡 **MEDIUM** — Can cause subtle bugs or maintenance issues
- 🔵 **LOW** — Code quality, style, or minor correctness issues

---

## Module-by-Module Findings

### 1. `erd_cache.cpp` / `erd_cache.h`

#### 🔴 CRITICAL: `erd_cache_destroy()` does not free heap-allocated entries

**File:** `erd_cache.cpp:67-72`

```cpp
void erd_cache_destroy(erd_cache_t* self)
{
  if (!self->initialized) return;
  erd_cache_init(self);  // ← resets entries without freeing ext_data
  self->initialized = false;
}
```

`erd_cache_init()` iterates entries and resets `valid=false`, `uses_heap=false`, but **never calls `delete[]` on `ext_data`** for entries where `uses_heap` was `true`. Every ERD entry with data > 4 bytes leaks its heap allocation on destroy.

**Impact:** Memory leak on every component teardown/rebuild. On ESP32 with limited RAM, repeated reboots or OTA updates will accumulate leaked memory.

**Fix:** In `erd_cache_init()` or a separate cleanup pass, iterate all entries and `delete[] e->ext_data` when `e->uses_heap` is true before resetting.

#### 🟡 MEDIUM: `erd_cache_update()` heap failure truncates data silently

**File:** `erd_cache.cpp:165-172`

When `new uint8_t[data_size]` fails, the code truncates to 4 bytes inline. This means the first 4 bytes of what could be a 32-byte string ERD are published as the complete value — silently corrupting data with no persistent error indication.

**Impact:** Truncated ERD values published to Home Assistant with no way for the user to detect the corruption.

**Fix:** Consider marking the entry as invalid or setting a persistent error flag when truncation occurs, rather than silently publishing partial data.

---

### 2. `erd_cache_mqtt_publisher.cpp`

#### 🟠 HIGH: Background task reads `publish_index` without mutex protection

**File:** `erd_cache_mqtt_publisher.cpp:60`

The background task calls `erd_cache_get_next_updated(self->cache, &self->publish_index)` without holding `state_mutex`. The `publish_index` field is part of the publisher struct, but the cache is shared with the main loop. If `tick_cooldowns()` or other cache operations run concurrently on single-core ESP32 during a context switch, the iterator could be corrupted.

**Impact:** Potential to skip entries or double-publish on ESP32 during context switches.

#### 🟠 HIGH: `mqtt_connected` default is `true` at init

**File:** `erd_cache_mqtt_publisher.cpp:132`

```cpp
self->mqtt_connected = true;
```

The publisher starts with `mqtt_connected = true` regardless of actual MQTT state. If the MQTT broker isn't connected yet when the publisher starts, it will attempt to publish to a disconnected client. The `esphome_mqtt_client_adapter.cpp` does call `notify_connected()` if already connected at init time (line 116-118), but there's a race: the adapter's `set_on_connect` callback fires asynchronously.

**Impact:** Initial publishes may be silently dropped or cause errors if MQTT isn't ready.

#### 🔵 LOW: `erd_cache_mqtt_publisher_loop()` allocates 512-byte stack buffer per call

**File:** `erd_cache_mqtt_publisher.cpp:333`

```cpp
char hex[512];
```

This is on the stack in the main loop path (non-ESP-IDF). For the background task path, pre-allocated buffers are used (`task_hex[512]`). The inconsistency means the non-ESP-IDF path uses more stack.

---

### 3. `erd_bridge_poll.cpp`

#### 🟠 HIGH: `send_cycle_reads()` can block main loop for 500ms+

**File:** `erd_bridge_poll.cpp:206-224`

The `POLL_CYCLE_SEND_BUDGET_MS = 500` constant allows the function to block for half a second before yielding. On single-core ESP32, this starves all other components. The function calls `esphome::delay(0)` to yield, but the outer loop continues processing batches.

**Impact:** Other ESPHome components (WiFi, MQTT, sensors) can be starved during large polling cycles.

#### 🟡 MEDIUM: `polling_failure_count` only increments when ALL ERDs fail

**File:** `erd_bridge_poll.cpp:102-109`

```cpp
if (self->cycle_has_failure) {
  self->polling_failure_count++;
} else {
  self->polling_failure_count = 0;
}
```

`cycle_has_failure` is set to `true` if *any* ERD in the cycle fails. But it's reset at cycle start. This means a single transient read failure in a cycle of 200 ERDs counts as a "failed cycle." Three such cycles triggers `state_failed`, which is overly aggressive for transient errors.

**Impact:** The polling bridge can transition to failed state on transient network noise, even if 99% of reads succeed.

#### 🔵 LOW: `erd_index` uses `(uint16_t)-1` as initial value

**File:** `erd_bridge_poll.cpp:301`

The `erd_index` is initialized to `(uint16_t)-1` (65535) in `state_probe_list` entry, then incremented to 0 in `send_next_read_request`. This is a confusing sentinel pattern — a simple `bool first_read` flag would be clearer.

---

### 4. `erd_bridge_subscribe.cpp`

#### 🟡 MEDIUM: `subscribe_failure_count` never resets on success

**File:** `erd_bridge_subscribe.cpp:93-97`

```cpp
self->subscribe_failure_count++;
if (self->subscribe_failure_count >= 3) {
  tiny_hsm_transition(hsm, state_failed);
}
```

The failure count increments on each failed subscribe attempt but is never reset when a subscribe succeeds. If the appliance has intermittent connectivity issues during startup, the counter could reach 3 even if the subscription eventually works, causing unnecessary fallback to polling.

**Impact:** In AUTO mode, subscription could be abandoned for polling even though it would eventually stabilize.

#### 🔵 LOW: Comment says "see erd_bridge_subscribe.h" in module goal

**File:** `erd_bridge_subscribe.h:18`

The "NOT responsible for" section references itself instead of `erd_bridge_poll.h`.

---

### 5. `feature_bit_manager.cpp`

#### 🟡 MEDIUM: Feature bit reads are not retried on failure

**File:** `feature_bit_manager.cpp:358-385`

When a feature bit ERD read fails, `skip_to_next_erd_()` advances to the next ERD without retry. This is different from `DeviceIdentityManager` which retries indefinitely. If a feature bit ERD fails due to transient bus noise, the resulting valid ERD list will be incomplete, potentially excluding ERDs that the appliance actually supports.

**Impact:** Incomplete feature bit data leads to fewer ERDs being polled/published, reducing the data available to Home Assistant.

#### 🔵 LOW: `parse_next_step_()` has hardcoded `parse_erd_idx_ < 10` boundary

**File:** `feature_bit_manager.cpp:469`

The parsing loop processes exactly 10 appliance ERDs (0x0093-0x0097, 0x0109-0x010D). If the GEA API adds more feature ERDs in the future, they would be silently ignored.

---

### 6. `device_identity_manager.cpp`

#### 🟡 MEDIUM: `bytes_to_string_()` interprets raw bytes as UTF-8

**File:** `device_identity_manager.cpp:95-104`

```cpp
std::string result(reinterpret_cast<const char*>(data), size);
```

Model numbers and serial numbers from the appliance are treated as null-terminated strings. If the appliance returns binary data or uses a different encoding, the resulting device ID could contain garbage or be truncated at the first null byte.

**Impact:** Device ID could be malformed, causing MQTT topic issues or duplicate device creation in Home Assistant.

#### 🔵 LOW: `sanitize_for_mqtt_topic_()` replaces all special chars with underscore

**File:** `device_identity_manager.cpp:106-119`

The sanitizer is thorough but could produce very long topic names if the model/serial numbers are long. MQTT topic length limits vary by broker.

---

### 7. `autodiscovery_manager.cpp`

#### 🟡 MEDIUM: No timeout on indefinite retry

**File:** `autodiscovery_manager.cpp:181-199`

The autodiscovery manager retries indefinitely with no backoff or timeout. If no appliance is present, the bridge will loop forever in the autodiscovery phase, never reaching the point where it could report an error to the user.

**Impact:** The component appears to work (no crash) but never produces any output. Users may not realize their appliance isn't responding.

#### 🔵 LOW: `schedule_next_broadcast_()` alternates GEA3/GEA2 without backoff

**File:** `autodiscovery_manager.cpp:181-199`

When both UARTs are configured and neither responds, the manager alternates between protocols with a fixed 5-second window. No exponential backoff means the bus is constantly polled even when no appliance is present.

---

### 8. `esphome_mqtt_client_adapter.cpp`

#### 🟠 HIGH: `device_id` is heap-allocated but adapter has no cleanup on re-init

**File:** `esphome_mqtt_client_adapter.cpp:96`

```cpp
self->device_id = new std::string(device_id);
```

If `esphome_mqtt_client_adapter_init()` is called multiple times (e.g., during a reconfiguration), the previous `device_id` string is leaked. The `destroy()` function properly deletes it, but re-init without destroy leaks.

**Impact:** Memory leak on reconfiguration.

#### 🟡 MEDIUM: Write payload buffer is shared state without synchronization

**File:** `esphome_mqtt_client_adapter.cpp:156-171`

The `write_payload_buffer_` and `write_payload_size_` fields are written in the MQTT subscribe callback, then read by the write bridge via `on_write_request_event`. On ESP-IDF, the MQTT callback runs in a different task than the main loop. If the write bridge reads the buffer while the callback is still writing, it could get a partially-decoded payload.

**Impact:** Corrupted write payloads could cause incorrect ERD writes to the appliance.

#### 🔵 LOW: `sscanf` used for hex parsing instead of manual decode

**File:** `esphome_mqtt_client_adapter.cpp:152-163`

`sscanf` with `%2x` format is used for hex decoding. This is significantly slower than a manual hex-to-byte lookup table, especially on embedded systems.

---

### 9. `erd_write_bridge.cpp`

#### 🟡 MEDIUM: Single write at a time with no queue

**File:** `erd_write_bridge.cpp:81-85`

```cpp
case signal_write_requested: {
  // Write already in progress — drop with warning.
  ESP_LOGW(TAG, "Write request for ERD 0x%04x dropped: write already in progress", args->erd);
} break;
```

Concurrent write requests are silently dropped. If Home Assistant sends multiple writes in quick succession (e.g., setting multiple settings), only the first one is processed.

**Impact:** User-initiated writes can be silently lost.

#### 🔵 LOW: Write bridge has no timeout for in-flight writes

**File:** `erd_write_bridge.cpp:76-115`

Once a write is queued, the bridge stays in `state_writing` until the ERD client reports completion or failure. If the client never reports back (e.g., appliance becomes unresponsive), the write bridge is permanently stuck, blocking all future writes.

---

### 10. `geappliances_bridge.cpp` (main component)

#### 🟠 HIGH: GEA2 tight loop runs for 200ms wall-clock time

**File:** `geappliances_bridge.cpp:311-354`

The GEA2 tight loop is a busy-wait that blocks the ESPHome main loop for 200ms. While `esp_task_wdt_reset()` is called inside the loop, this still blocks all other ESPHome components for a significant duration. The hard cap of 400ms provides a safety net but doesn't address the fundamental issue.

**Impact:** WiFi, MQTT, and other components can be starved for up to 200ms per loop iteration when GEA2 is active.

#### 🟡 MEDIUM: `custom_erds_` array has fixed 64-entry limit

**File:** `geappliances_bridge.h:165-167`

```cpp
static constexpr uint16_t CUSTOM_ERDS_MAX = 64;
```

If a user configures more than 64 custom ERDs, extras are silently dropped with no warning.

#### 🔵 LOW: Unused `loop_start` variable shadowed

**File:** `geappliances_bridge.cpp:294,301`

`loop_start` is declared at line 294, then `loop_start_ms` is declared at line 301 inside the GEA2 block. The outer `loop_start` is used at line 388 for elapsed time calculation, but the naming is confusing.

---

### 11. `geappliances_bridge_bridge_init.cpp`

#### 🟡 MEDIUM: `handle_subscription_failed()` destroys and reinitializes bridges synchronously

**File:** `geappliances_bridge_bridge_init.cpp:360-410`

When subscription fails, the function destroys the subscription bridge, destroys any existing polling bridge, builds a new poll list, and initializes a new polling bridge — all in one call. This is a significant amount of work done synchronously in the main loop, including memory allocation for the new bridge's internal structures.

**Impact:** Brief main loop blocking during the transition.

#### 🔵 LOW: `on_discovery_complete` callback is set multiple times

**File:** `geappliances_bridge_bridge_init.cpp:202-205,290-293,385-388`

The `on_discovery_complete` callback is set in three different code paths. While functionally correct (it's always the same callback), the repetition suggests a single initialization point would be cleaner.

---

### 12. `geappliances_bridge_startup_hsm.cpp`

#### 🟡 MEDIUM: `g_bridge_services` is a global pointer

**File:** `geappliances_bridge_startup_hsm.cpp:32`

```cpp
static IBridgeServices* g_bridge_services = nullptr;
```

The startup HSM uses a global pointer to access bridge services. This prevents having multiple bridge instances (not a concern for ESPHome, but it's a design smell). More importantly, if `set_bridge_services()` is never called or is called with a stale pointer, all HSM operations will crash.

**Impact:** Crash if the HSM is used before `set_bridge_services()` is called, or after the bridge is destroyed.

#### 🔵 LOW: `startup_state_mqtt_client_init` does everything in entry

**File:** `geappliances_bridge_startup_hsm.cpp:242-261`

The MQTT client init, ERD cache publisher init, and feature bit reading start all happen in the entry handler, then immediately transitions to the next state. This means the state has no `signal_run_loop` handler — it's purely a transition state.

---

### 13. `erd_poll_list_builder.cpp`

#### 🔵 LOW: `deduplicate()` sorts the ERD list

**File:** `erd_poll_list_builder.cpp:27-38`

The deduplication step sorts the ERD list, which changes the order from the original group ordering. The comment says "standard ERDs first (in their original group order), then custom ERDs," but sorting destroys the group ordering.

**Impact:** ERDs are polled in sorted order rather than the intended group order. This is cosmetic but could affect which ERDs are probed first during discovery.

---

### 14. `__init__.py` (ESPHome component)

#### 🟠 HIGH: Unpinned library dependencies

**File:** `__init__.py:324-325`

```python
cg.add_library("https://github.com/ryanplusplus/tiny", None)
cg.add_library("https://github.com/geappliances/tiny-gea-api#develop", None)
```

The `tiny` library has no version pin at all, and `tiny-gea-api` tracks the `develop` branch. Any upstream change — including breaking API changes — will silently break the build. The comment on line 327-330 acknowledges this risk but doesn't mitigate it.

**Impact:** Builds can break unexpectedly when upstream libraries change.

#### 🟡 MEDIUM: `load_appliance_types()` fetches from GitHub at compile time

**File:** `__init__.py:173-207`

If the local JSON file isn't found, the component makes an HTTP request to GitHub during ESPHome compilation. This adds a network dependency to the build process and can fail due to network issues, rate limiting, or GitHub downtime.

**Impact:** Build failures when offline or when GitHub is unavailable.

#### 🔵 LOW: `generate_appliance_type_function()` generates a large switch statement

**File:** `__init__.py:238-258`

The generated function creates a switch statement with one case per appliance type. With many appliance types, this generates a large function that could impact compile time and binary size.

---

### 15. `erd_bridge_common.h`

#### 🔵 LOW: `erd_set_insert()` uses O(n) shift for sorted insertion

**File:** `erd_bridge_common.h:137-160`

The `erd_set_insert()` function does a binary search for the position, then shifts all subsequent elements to make room. For a set of 645 elements, this is O(n) per insert. Given that the set is populated during discovery (one-time), this is acceptable, but the comment could note the complexity.

---

### 16. `esphome_uart_adapter.cpp`

#### 🔵 LOW: Poll timer period is 0 (as fast as possible)

**File:** `esphome_uart_adapter.cpp:86`

```cpp
tiny_timer_start_periodic(timer_group, &self->timer, 0, self, poll);
```

A period of 0 means the timer fires on every `tiny_timer_group_run()` call. With two UART adapters in the shared timer group, this means both poll callbacks fire on every timer group run, even though only one is active at a time.

---

## Architecture Observations

### Strengths

1. **Clear module boundaries:** Each module has a well-defined goal, responsibilities, and dependencies documented in the header comments.
2. **State machine design:** The use of `tiny_hsm` for startup, polling, subscription, and write operations provides clear state transitions and makes the code easier to reason about.
3. **Fixed-capacity data structures:** The use of fixed arrays instead of dynamic containers (std::vector, std::set) eliminates heap allocation in the hot path.
4. **Self-driving managers:** Managers like `FeatureBitManager` and `AutodiscoveryManager` own their own timers and event subscriptions, reducing coupling with the main bridge.
5. **Comprehensive test coverage:** 347 tests covering all major modules with good use of doubles/mocks.
6. **Dual protocol support:** Clean abstraction between GEA2 and GEA3 through the adapter pattern.

### Concerns

1. **Main loop blocking:** Multiple paths (GEA2 tight loop, polling cycle sends, subscription fallback) can block the main loop for significant durations.
2. **Memory management:** Several places where heap allocations are not properly cleaned up on destroy or re-init.
3. **Error handling:** Transient failures in some paths (feature bit reads, autodiscovery) are not retried, leading to permanent degradation.
4. **Global state:** The startup HSM uses a global pointer to access bridge services, which is a code smell even if it works for the single-instance ESPHome model.

---

## Recommendations by Priority

### Immediate (should fix before next release)

1. **Fix `erd_cache_destroy()` to free heap-allocated entries** — prevents memory leaks on teardown
2. **Pin library dependencies** — prevent silent build breakage from upstream changes
3. **Reset `subscribe_failure_count` on successful subscription** — prevent premature fallback to polling

### Short-term (next sprint)

4. **Add retry logic for feature bit ERD reads** — prevent incomplete ERD lists from transient failures
5. **Add write bridge timeout** — prevent permanent stuck state on unresponsive appliances
6. **Add autodiscovery timeout or backoff** — provide user feedback when no appliance is found
7. **Synchronize write payload buffer access** — prevent race condition between MQTT callback and write bridge

### Long-term (future improvements)

8. **Reduce main loop blocking** — consider moving GEA2 tight loop to a background task
9. **Queue write requests** — support concurrent writes from Home Assistant
10. **Add diagnostic sensors for bridge health** — expose polling failure count, subscription state, etc.

---

## Test Coverage Assessment

| Module | Test File | Coverage Assessment |
|--------|-----------|-------------------|
| ERD Cache | `erd_cache_test.cpp` | Good — covers insert, update, throttle, iteration |
| Poll Bridge | `erd_bridge_poll_test.cpp` | Good — covers probe, polling, failure, recovery |
| Subscribe Bridge | `erd_bridge_subscribe_test.cpp` | Good — covers subscribe, retain, quiet, fail |
| Write Bridge | `erd_write_bridge_test.cpp` | Good — covers write flow, failure, concurrent |
| Feature Bit Manager | `feature_bit_manager_test.cpp` | Good — covers read sequence, parsing, skip |
| Device Identity | `device_identity_manager_test.cpp` | Good — covers ERD reads, ID generation |
| Autodiscovery | `autodiscovery_manager_test.cpp` | Good — covers GEA3/GEA2 discovery, fallback |
| Startup HSM | `startup_hsm_test.cpp` | Good — covers phase transitions |
| MQTT Adapter | `esphome_mqtt_client_adapter_test.cpp` | Good — covers publish, write topic |
| Cache Publisher | `erd_cache_mqtt_publisher_test.cpp` | Good — covers publish loop, connect/disconnect |
| ERD Registry | `erd_registry_test.cpp` | Good — covers valid/registered ERD tracking |
| Poll List Builder | `erd_poll_list_builder_test.cpp` | Good — covers all mode combinations |
| Integration | `write_flow_integration_test.cpp` | Good — end-to-end write flow |
| Simulation | `application_level_test.cpp`, `configuration_tests.cpp` | Good — realistic scenarios |

**Gap:** No test for `erd_cache_destroy()` freeing heap entries. No test for the ESP-IDF background task path (only stubbed). No test for `handle_subscription_failed()` full reinitialization flow.

---

*End of review.*
