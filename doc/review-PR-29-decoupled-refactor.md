# PR #29 Review: Decoupled Appliance/MQTT Refactor

**Reviewer:** Assistant
**Date:** 2026-06-19
**Branch:** decoupleFeatureBranch
**Scope:** ~80 files — shared ERD cache, MQTT adapter decoupling, write bridge, startup HSM, feature bit manager

---

## Summary

The architectural direction is sound — decoupling MQTT from ESPHome internals and introducing a shared cache is the right move. The code quality is generally high: good use of `std::nothrow`, proper cleanup in destroy functions, and well-structured HSM states. However, there are several issues ranging from correctness bugs to test gaps that should be addressed before merge.

---

## Critical Issues

### 1. **FIXED** `data_changed` comparison refactored (erd_cache.cpp)

**Status:** ✅ Resolved on branch `review/PR-29-decoupled-refactor-review`

Replaced the fragile partial `memcmp` with a clear `erd_data_changed()` helper:
```cpp
static bool erd_data_changed(const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size)
{
  if (existing->data_size != new_size) return true;
  const uint8_t* old = existing->uses_heap ? existing->heap_data : existing->inline_data;
  return memcmp(old, new_data, new_size) != 0;
}
```

Also removed the dead `memcmp` in the OOM truncation path (was comparing data just `memcpy`'d from itself).

Added 6 new tests covering: same-size same data, same-size different data, size shrink, size grow, inline-to-heap promotion, heap-to-inline shrink. All 239 tests pass.

### 2. **FIXED** Heap allocation failure path redundant `memcmp` (erd_cache.cpp)

**Status:** ✅ Resolved on branch `review/PR-29-decoupled-refactor-review`

The dead `memcmp` was removed and replaced with `existing->update_required = true` — truncation always changes the effective data (size shrinks). The OOM path now returns `true` directly, simplifying the logic.
### 3. No re-publish of retained messages after MQTT reconnect (erd_cache_mqtt_publisher.cpp:128-132)

After disconnect/reconnect, the publisher resumes only entries with `update_required=true`. But retained MQTT messages on the broker are lost when the broker restarts or the client reconnects with a new client ID. There is no mechanism to force a full re-publish of all cached ERDs after reconnect. Home Assistant entities can show stale data after a broker restart.

### 4. `g_bridge_services` is a file-scope global pointer (geappliances_bridge_startup_hsm.cpp:31)

```cpp
static IBridgeServices* g_bridge_services = nullptr;
```

This is a singleton assumption baked into the startup HSM. The `services_from_hsm()` function ignores the `hsm` parameter entirely. If multiple bridge instances are ever needed (e.g., for dual UART setups), this breaks.

---

## Significant Issues

### 5. **FIXED** Hex encoding is lowercase; test name corrected (erd_cache_mqtt_publisher.cpp:113)

**Status:** ✅ Resolved on branch `review/PR-29-decoupled-refactor-review`

Lowercase hex (`%02x`) is the correct output. Renamed the misleading test from `payload_uppercase_hex_no_separator` to `payload_lowercase_hex_no_separator`.

### 6. `erd_cache_mqtt_publisher` defaults `mqtt_connected = true` (erd_cache_mqtt_publisher.cpp:28)

```cpp
self->mqtt_connected = true;
```

The publisher assumes MQTT is connected at init time, but the MQTT client adapter may not be connected yet. The adapter's `publish_raw` checks `is_connected()` and returns early, so this is a no-op — but it wastes loop iterations and gives a false sense of publishing.

### 7. `erd_index` initialized to `(uint16_t)-1` (erd_bridge_poll.cpp:321)

```cpp
self->erd_index = (uint16_t)-1;  // 65535
```

Then `send_next_read_request()` increments first, wrapping to 0. This is correct but fragile — if `erd_index` is ever initialized to anything other than `(uint16_t)-1`, the first ERD is skipped or an out-of-bounds access occurs.

### 8. `s_overflow_warned` is never reset (erd_cache.cpp:14)

```cpp
static bool s_overflow_warned = false;
```

Once the cache overflows, this flag suppresses all future overflow warnings permanently, even across `erd_cache_destroy()`/`erd_cache_init()` cycles.

### 9. `polling_only_publish_on_change` default changed from `false` to `true` (__init__.py:301)

This is a breaking behavioral change for existing users. Entities that depend on periodic refresh (even when values haven't changed) will go stale in Home Assistant.

### 10. PR description claims `request_timeout` increased to 500ms, but code still shows 250ms (geappliances_bridge.cpp:18,27)

```cpp
.request_timeout = 250,  // still 250, not 500
```

Either the description is wrong or the change was not included.

### 11. Stale comment on `erd_cache_get_next_updated()` (erd_cache.h:65-66)

The comment says "for future use" but the function is actively used by `erd_cache_mqtt_publisher_loop()`.

### 12. `publish_ha_discovery_()` is a no-op in non-ESP-IDF builds (ha_discovery_manager.cpp:168-170)

```cpp
void HaDiscoveryManager::publish_ha_discovery_()
{
  ESP_LOGD(TAG, "HA discovery triggered (no MQTT broker — skipping entity publish)");
  this->state_ = HA_DISCOVERY_COMPLETE;
}
```

HA discovery is completely non-functional in test/simulation builds. The test suite doesn't catch this because simulation tests don't assert on discovery output.

---

## Minor Issues

### 13. Topic truncation permanently blocks all publishes (erd_cache_mqtt_publisher.cpp:103-108)

When a device_id is too long for the topic buffer, the function returns immediately. Every subsequent loop iteration will hit the same truncation, meaning no ERDs are ever published.

### 14. Slow read warning fires unconditionally (erd_bridge_poll.cpp:181-184)

Every read taking >500ms generates a warning. With 100+ ERDs, this can spam the log.

### 15. 512-byte hex buffer on stack per loop iteration (erd_cache_mqtt_publisher.cpp:111)

```cpp
char hex[512];
```

Significant stack usage in a tight loop. Consider using a static buffer or heap allocation.

### 16. `std::set<tiny_erd_t>*` stored as `void*` (erd_bridge_subscribe.cpp:150, erd_bridge_poll.cpp)

The `void*` cast obscures the type and makes static analysis harder.

---

## Test Coverage Gaps

| # | Gap |
| 17 | **FIXED** No test for `erd_cache_update()` with data > 16 bytes (heap path) |
| 18 | No test for `erd_cache_update()` heap allocation failure (OOM path) |
| 19 | No test for cache overflow (200+ ERDs) |
| 20 | No test for `erd_cache_mqtt_publisher_loop()` with `max_ms = 0` |
| 21 | **FIXED** `payload_uppercase_hex_no_separator` doesn't verify payload content |
| 22 | No integration test for the full startup HSM flow |
| 23 | No test for MQTT reconnect re-publish behavior |
| 24 | No test for `erd_write_bridge` with concurrent write requests |

---

## Recommendation

**Do not merge until issues #3, #4, #9, and #10 are addressed.** The remaining issues should be tracked as follow-up tasks.

Issues #3 (no re-publish after reconnect) and #4 (global singleton) are architectural — they affect correctness and extensibility. Issues #9 (breaking default change) and #10 (stale PR description) affect users directly.
