# Critical Issues: refinement vs develop

Branch: `refinement` · Compared against: `develop` · Date: 2026-06-17
Status: **All 10 issues from the original review remain unfixed.**

---

## 1. CRITICAL — `registered_erds_snapshot_` never populated — HA discovery fetches only "common" category

**File:** `ha_discovery_manager.cpp:46-49` (`set_registered_erds`), `ha_discovery_manager.h:134` (declaration)

**Status: UNFIXED**

`set_registered_erds()` copies to `registered_erds_` but **never** to `registered_erds_snapshot_`. The snapshot is used at lines 225 and 294–298 to determine which JSONL categories to fetch and to filter ERDs during parsing. Since it is always empty, only the "common" category is ever fetched — HA discovery entities for refrigeration, laundry, dishwasher, range, etc. are never populated on ESP-IDF builds.

**Evidence:** `set_registered_erds()` body (lines 46–49) assigns only `registered_erds_`; no assignment to `registered_erds_snapshot_` exists anywhere in the file.

**Fix:** Add `this->registered_erds_snapshot_ = erds;` to `set_registered_erds()`.

---

## 2. CRITICAL — `memcmp` buffer over-read in `erd_cache_update()`

**File:** `erd_cache.cpp:59-61`

**Status: UNFIXED**

```c
bool data_changed = (existing->data_size != data_size) ||
                    (memcmp(existing->uses_heap ? existing->heap_data : existing->inline_data,
                            data, data_size) != 0);
```

When `data_size` differs from `existing->data_size`, `memcmp` uses the new `data_size` against the old buffer. Example: existing entry has 8 bytes inline, new data is 20 bytes — `memcmp` reads 4 bytes past `inline_data[16]`. This is a stack/heap buffer over-read (undefined behavior).

The `||` short-circuit *should* prevent this when sizes differ, but the expression is evaluated as a whole in the `bool` assignment — the `memcmp` is still reachable if the compiler does not optimize the dead branch, and more importantly, if `data_size` is *smaller* than `existing->data_size`, the size check passes (they're not equal) and `memcmp` reads `data_size` bytes from the old buffer, which *is* safe. The real danger is when `data_size > existing->data_size` and the compiler does not short-circuit. However, C standard guarantees left-to-right evaluation of `||` with a sequence point — so if `existing->data_size != data_size` is true, the `memcmp` is NOT evaluated. **This is actually safe due to short-circuit evaluation.**

**Revised assessment:** The `||` short-circuit in C guarantees that when sizes differ, `memcmp` is not evaluated. When sizes are equal, `memcmp` reads exactly `data_size` bytes from a buffer of that same size — safe. **This issue is a false positive.**

**Status: FALSE POSITIVE — Not a real issue.**

---

## 3. CRITICAL — `erd_cache_mqtt_publisher_init()` null dereference on NULL `mqtt_client`

**File:** `erd_cache_mqtt_publisher.cpp:32-51`

**Status: UNFIXED**

`init()` calls `mqtt_client_on_mqtt_disconnect(self->mqtt_client)` and `mqtt_client_on_mqtt_connect(self->mqtt_client)` at lines 38–39 and 49–50 without null-checking `mqtt_client`. These inline wrappers dereference `self->api->...` — a null pointer dereference.

**Evidence:** Lines 38–51 call `mqtt_client_on_mqtt_disconnect()` and `mqtt_client_on_mqtt_connect()` directly on `self->mqtt_client` with no guard.

**Fix:** Add `if (!mqtt_client) return;` guard after the `memset` and field assignments, before the event subscriptions.

---

## 4. CRITICAL — `erd_cache_mqtt_publisher_loop()` null function pointer dereference

**File:** `erd_cache_mqtt_publisher.cpp:79-87`

**Status: UNFIXED**

The guard at line 79 checks `cache`, `mqtt_client`, and `device_id` but NOT `get_time_ms`. If `init()` was never called (struct is zeroed by memset), `get_time_ms` is null. Line 87 calls `self->get_time_ms()` — crash.

**Evidence:** Line 79 guard: `if (!self->cache || !self->mqtt_client || !self->device_id)` — no `get_time_ms` check. Line 87: `uint32_t start_ms = self->get_time_ms();`.

**Fix:** Add `|| !self->get_time_ms` to the guard condition at line 79.

---

## 5. CRITICAL — `erd_cache_mqtt_publisher_destroy()` leaks event subscriptions on early return

**File:** `erd_cache_mqtt_publisher.cpp:56-72`

**Status: UNFIXED**

If `!self->cache` is true, the function returns immediately at line 59 without unsubscribing from MQTT events. If `mqtt_client` is non-null (partial init state after `memset` + field assignments but before event subscriptions), the subscriptions are not yet created, so this is actually safe for the *init-then-destroy* path. However, if `mqtt_client` was set but subscriptions were partially created (e.g., disconnect subscription succeeded but connect subscription threw), the early return leaks the disconnect subscription.

**Revised assessment:** The early return at line 58–59 checks `!self->cache`. If `init()` was called, both `cache` and `mqtt_client` would be set and subscriptions created — so the early return path only fires when `init()` was never called or `cache` was explicitly set to null after init. In the never-initialized case, subscriptions were never created, so there's nothing to leak. **This is actually safe for the normal usage pattern.**

However, there is a subtle issue: if `init()` is called with a valid `cache` but NULL `mqtt_client`, the code will crash at line 38 (issue #3). If it somehow gets past that (e.g., `mqtt_client` is non-null but its `api` pointer is null), the subscriptions could be in an inconsistent state when `destroy()` is called. The early return on `!self->cache` doesn't address this.

**Status: PARTIAL FALSE POSITIVE — The specific early-return-leak scenario is unlikely, but the function should guard `!self` and handle partial-init states more robustly.**

---

## 6. CRITICAL — `new[]` without null check — crash on heap exhaustion

**File:** `erd_cache.cpp:72,119`

**Status: UNFIXED**

`new uint8_t[data_size]` at lines 72 and 119 throws `std::bad_alloc` on ESP32 heap exhaustion. No try/catch. On embedded systems with constrained heap, this is a crash risk when large ERD payloads arrive.

**Evidence:** Both `new` calls use standard `new` with no `std::nothrow` qualifier and no exception handling.

**Fix:** Use `new (std::nothrow) uint8_t[data_size]` and check for null before `memcpy`, or add try/catch around the allocation.

---

## 7. HIGH — Subscription fallback path — `on_discovery_complete` callback not set

**File:** `geappliances_bridge_bridge_init.cpp:352-359`

**Status: UNFIXED**

When AUTO mode falls back from subscription to polling, `mqtt_bridge_polling_init()` sets `on_discovery_complete = nullptr`. The callback is only wired in `initialize_mqtt_bridge_()` (lines 178–183) for the initial polling path. The fallback polling bridge never calls `ha_discovery_manager_.set_registered_erds()`. Combined with issue #1, HA discovery is doubly broken in this path.

**Evidence:** `check_subscription_activity_()` calls `mqtt_bridge_polling_init()` at line 352 but does not set `on_discovery_complete` or `on_discovery_complete_context` afterward. `mqtt_bridge_polling_init_impl()` sets both to `nullptr` at lines 742–743.

**Fix:** After `mqtt_bridge_polling_init()` at line 352, set the `on_discovery_complete` callback and context the same way `initialize_mqtt_bridge_()` does (lines 178–183).

---

## 8. MEDIUM — `FeatureBitManager` has no cleanup/destroy — potential use-after-free on re-init

**File:** `feature_bit_manager.h`, `feature_bit_manager.cpp`

**Status: UNFIXED**

Subscribes to ERD client activity events in `init()` but has no `destroy()`/`cleanup()` method to unsubscribe. If the bridge is torn down and re-initialized, old subscriptions remain active.

**Evidence:** No `destroy`, `cleanup`, or `unsubscribe` method exists in the header or implementation.

---

## 9. MEDIUM — `__init__.py` `load_appliance_types()` fetches from GitHub at compile time

**File:** `__init__.py:182-186`

**Status: UNFIXED**

`urllib.request.urlopen(url, timeout=5)` at line 186 fetches from GitHub during ESPHome compilation. Builds are non-deterministic and can fail/timeout if GitHub is unreachable.

**Evidence:** Lines 182–202 still contain the `urllib.request.urlopen` call with a 5-second timeout.

---

## 10. LOW — Silent topic truncation

**File:** `erd_cache_mqtt_publisher.cpp:103-104`

**Status: UNFIXED**

`snprintf(topic, sizeof(topic), ...)` with 128-byte buffer silently truncates long device IDs. The truncated topic is published without any indication of corruption.

**Evidence:** Line 103–104 unchanged.

---

## Summary

| # | Severity | Status | File | Issue |
|---|----------|--------|------|-------|
| 1 | **CRITICAL** | **UNFIXED** | ha_discovery_manager.cpp | `registered_erds_snapshot_` never set — HA discovery broken for non-common categories |
| 2 | ~~CRITICAL~~ | **FALSE POSITIVE** | erd_cache.cpp:59-61 | `memcmp` over-read — short-circuit `||` makes this safe |
| 3 | **CRITICAL** | **UNFIXED** | erd_cache_mqtt_publisher.cpp:32-51 | Null dereference in `init()` on NULL `mqtt_client` |
| 4 | **CRITICAL** | **UNFIXED** | erd_cache_mqtt_publisher.cpp:79-87 | Null function pointer dereference in `loop()` |
| 5 | ~~CRITICAL~~ | **PARTIAL FP** | erd_cache_mqtt_publisher.cpp:56-72 | Early return in `destroy()` — unlikely to trigger, but defensive guard missing |
| 6 | **CRITICAL** | **UNFIXED** | erd_cache.cpp:72,119 | Unhandled `new[]` exception on heap exhaustion |
| 7 | **HIGH** | **UNFIXED** | bridge_init.cpp:352-359 | Fallback polling bridge has no `on_discovery_complete` callback |
| 8 | **MEDIUM** | **UNFIXED** | feature_bit_manager.cpp | No cleanup/destroy — potential UAF on re-init |
| 9 | **MEDIUM** | **UNFIXED** | __init__.py:182-186 | Network call during compilation |
| 10 | **LOW** | **UNFIXED** | erd_cache_mqtt_publisher.cpp:103 | Silent topic truncation |

**Remaining actionable issues: 8** (issues 1, 3, 4, 5, 6, 7, 8, 9, 10)
- Issue 2 is a false positive (short-circuit evaluation prevents the over-read).
- Issue 5 is a partial false positive (the specific leak scenario is unlikely, but a defensive `!self` guard is still recommended).

Issues **1, 3, 4, 6, 7** should be fixed before merge. Issues **5, 8, 9, 10** are lower priority.
