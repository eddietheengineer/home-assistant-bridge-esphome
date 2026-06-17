# Critical Issues: addBackMqtt vs develop

Branch: `addBackMqtt` · Compared against: `develop` · Date: 2026-06-17

---

## 1. CRITICAL — `registered_erds_snapshot_` never populated — HA discovery fetches only "common" category

**File:** `ha_discovery_manager.cpp:46-49` (`set_registered_erds`), `ha_discovery_manager.h:134` (declaration)

`set_registered_erds()` copies to `registered_erds_` but **never** to `registered_erds_snapshot_`. The snapshot is used at lines 225 and 294–298 to determine which JSONL categories to fetch and to filter ERDs during parsing. Since it is always empty, only the "common" category is ever fetched — HA discovery entities for refrigeration, laundry, dishwasher, range, etc. are never populated on ESP-IDF builds.

**Fix:** Add `this->registered_erds_snapshot_ = erds;` to `set_registered_erds()`.

---

## 2. CRITICAL — `memcmp` buffer over-read in `erd_cache_update()`

**File:** `erd_cache.cpp:59-61`

```c
bool data_changed = (existing->data_size != data_size) ||
                    (memcmp(existing->uses_heap ? existing->heap_data : existing->inline_data,
                            data, data_size) != 0);
```

When `data_size` differs from `existing->data_size`, `memcmp` uses the new `data_size` against the old buffer. Example: existing entry has 8 bytes inline, new data is 20 bytes — `memcmp` reads 4 bytes past `inline_data[16]`. This is a stack/heap buffer over-read (undefined behavior).

**Fix:** Use `min(existing->data_size, data_size)` for the `memcmp` length, or short-circuit when sizes differ (they already do via the `||`, but the C short-circuit does not prevent the `memcmp` from being evaluated in all compilers due to the `||` being part of a larger expression — verify compiler behavior or restructure).

---

## 3. CRITICAL — `erd_cache_mqtt_publisher_init()` null dereference on NULL `mqtt_client`

**File:** `erd_cache_mqtt_publisher.cpp:32-51`

`init()` calls `mqtt_client_on_mqtt_disconnect(self->mqtt_client)` and `mqtt_client_on_mqtt_connect(self->mqtt_client)` without null-checking `mqtt_client`. These inline wrappers dereference `self->api->...` — a null pointer dereference.

**Fix:** Add `if (!mqtt_client) return;` guard at the top of `init()`.

---

## 4. CRITICAL — `erd_cache_mqtt_publisher_loop()` null function pointer dereference

**File:** `erd_cache_mqtt_publisher.cpp:79-87`

The guard at line 79 checks `cache`, `mqtt_client`, and `device_id` but NOT `get_time_ms`. If `init()` was never called (struct is zeroed by memset), `get_time_ms` is null. Line 87 calls `self->get_time_ms()` — crash.

**Fix:** Add `!self->get_time_ms` to the guard condition.

---

## 5. CRITICAL — `erd_cache_mqtt_publisher_destroy()` leaks event subscriptions on early return

**File:** `erd_cache_mqtt_publisher.cpp:56-72`

If `!self->cache` is true, the function returns immediately without unsubscribing from MQTT events. If `mqtt_client` is non-null (partial init state), the subscriptions remain dangling — callbacks will fire against freed/zeroed memory.

**Fix:** Check `!self` first, then always unsubscribe if `mqtt_client` is non-null, regardless of `cache` state.

---

## 6. CRITICAL — `new[]` without null check — crash on heap exhaustion

**File:** `erd_cache.cpp:72,119`

`new uint8_t[data_size]` throws `std::bad_alloc` on ESP32 heap exhaustion. No try/catch. On embedded systems with constrained heap, this is a crash risk when large ERD payloads arrive.

**Fix:** Use `new (std::nothrow)` and check for null, or add try/catch.

---

## 7. HIGH — Subscription fallback path — `on_discovery_complete` callback not set

**File:** `geappliances_bridge_bridge_init.cpp:352-359`

When AUTO mode falls back from subscription to polling, `mqtt_bridge_polling_init()` sets `on_discovery_complete = nullptr`. The callback is only wired in `initialize_mqtt_bridge_()` (lines 178–183) for the initial polling path. The fallback polling bridge never calls `ha_discovery_manager_.set_registered_erds()`. Combined with issue #1, HA discovery is doubly broken in this path.

**Fix:** Set the `on_discovery_complete` callback in `check_subscription_activity_()` after re-initializing the polling bridge.

---

## 8. MEDIUM — `FeatureBitManager` has no cleanup/destroy — potential use-after-free on re-init

**File:** `feature_bit_manager.cpp`

Subscribes to ERD client activity events in `init()` but has no `destroy()`/`cleanup()` method. If the bridge is torn down and re-initialized, old subscriptions remain active.

---

## 9. MEDIUM — `__init__.py` `load_appliance_types()` fetches from GitHub at compile time

**File:** `__init__.py:119-174`

Network call during ESPHome compilation. Builds are non-deterministic and can fail/timeout if GitHub is unreachable.

---

## 10. LOW — Silent topic truncation

**File:** `erd_cache_mqtt_publisher.cpp:103-104`

`snprintf(topic, sizeof(topic), ...)` with 128-byte buffer silently truncates long device IDs. The truncated topic is published without any indication of corruption.

---

## Summary

| # | Severity | File | Issue |
|---|----------|------|-------|
| 1 | **CRITICAL** | ha_discovery_manager.cpp | `registered_erds_snapshot_` never set — HA discovery broken for non-common categories |
| 2 | **CRITICAL** | erd_cache.cpp:59-61 | `memcmp` buffer over-read (UB) |
| 3 | **CRITICAL** | erd_cache_mqtt_publisher.cpp:32-51 | Null dereference in `init()` |
| 4 | **CRITICAL** | erd_cache_mqtt_publisher.cpp:79-87 | Null function pointer dereference in `loop()` |
| 5 | **CRITICAL** | erd_cache_mqtt_publisher.cpp:56-72 | Dangling event subscriptions on early return from `destroy()` |
| 6 | **CRITICAL** | erd_cache.cpp:72,119 | Unhandled `new[]` exception on heap exhaustion |
| 7 | **HIGH** | bridge_init.cpp:352-359 | Fallback polling bridge has no `on_discovery_complete` callback |
| 8 | **MEDIUM** | feature_bit_manager.cpp | No cleanup/destroy — potential UAF on re-init |
| 9 | **MEDIUM** | __init__.py:119-174 | Network call during compilation |
| 10 | **LOW** | erd_cache_mqtt_publisher.cpp:103 | Silent topic truncation |

Issues **1–6** should be fixed before merge. Issue **7** is a functional regression in AUTO mode fallback. Issues **8–10** are lower priority but should be addressed.
