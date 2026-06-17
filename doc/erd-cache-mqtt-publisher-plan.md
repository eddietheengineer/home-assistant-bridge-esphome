# ERD Cache to MQTT Publisher Plan

## Goal

Connect the ERD cache to MQTT publishing via a standalone module that scans the cache each loop() iteration and publishes updated ERD values to `geappliances/{deviceId}/erd/0x{ERD}/value` topics with `retain=true`. The cache is elevated to `GeappliancesBridge` level so both bridge HSMs (polling and subscription) write to a single shared instance.

## Constraints

- Decoupling: The new publisher module depends only on `erd_cache.h`, `i_mqtt_client.h`, and ESPHome logging. No dependency on bridge internals, HSMs, or appliance protocol code.
- No blocking: Publishing respects a time budget (10ms) and hard cap (20 publishes) per loop() call.
- Round-robin fairness: A persistent index ensures all ERDs in the cache get published over time.
- Disconnect handling: Publisher stops on MQTT disconnect, resumes on reconnect, with warning logs.
- Write commands: Out of scope for this plan.
- Naming: `mqtt_bridge_t` / `mqtt_bridge_polling_t` rename deferred to a future pass.

## Architecture

```
[Appliance] -> [ERD Client] -> [Polling HSM] \
                                     -> [Shared ERD Cache] -> [Publisher] -> [MQTT]
                              [Subscription HSM] /
```

## Phase 1: Elevate ERD Cache to GeappliancesBridge

### 1.1 Add shared cache to GeappliancesBridge

**`geappliances_bridge.h`**
- Add `#include "erd_cache.h"` in the extern "C" block
- Add `erd_cache_t erd_cache_;` as a protected member
- Add `void publish_erd_cache_();` as a protected method

### 1.2 Initialize/destroy cache at bridge level

**`geappliances_bridge.cpp`**
- In `setup()`, after `tiny_timer_group_init()`: call `erd_cache_init(&this->erd_cache_);`
- In `teardown()`, before bridge destroys: call `erd_cache_destroy(&this->erd_cache_);`

### 1.3 Remove embedded cache from mqtt_bridge_t

**`mqtt_bridge.h`**
- Replace `erd_cache_t erd_cache;` with `erd_cache_t* erd_cache;` (pointer)

**`mqtt_bridge.cpp`**
- Change `mqtt_bridge_init()` signature to add `erd_cache_t* cache` parameter after `address`
- Store pointer: `self->erd_cache = cache;`
- Remove `erd_cache_init(&self->erd_cache);` from init
- Change all `&self->erd_cache` references to `self->erd_cache` (already a pointer)
- Remove `erd_cache_destroy(&self->erd_cache);` from destroy

### 1.4 Remove embedded cache from mqtt_bridge_polling_t

**`mqtt_bridge_polling.h`**
- Replace `erd_cache_t erd_cache;` with `erd_cache_t* erd_cache;` (pointer)

**`mqtt_bridge_polling.cpp`**
- Change `mqtt_bridge_polling_init()` and `mqtt_bridge_polling_init_at_address()` signatures to add `erd_cache_t* cache` parameter
- Update `mqtt_bridge_polling_init_impl()` to accept and store the cache pointer
- Remove `erd_cache_init(&self->erd_cache);` from init
- Change all `&self->erd_cache` references to `self->erd_cache`
- Change `erd_cache_init(&self->erd_cache)` calls in state handlers (re-entry after appliance lost) to `erd_cache_init(self->erd_cache)`
- Remove `erd_cache_destroy(&self->erd_cache);` from destroy

### 1.5 Wire the shared cache in bridge initialization

**`geappliances_bridge_bridge_init.cpp`**
- In `initialize_mqtt_bridge_()`, pass `&this->erd_cache_` to both `mqtt_bridge_init()` and `mqtt_bridge_polling_init()` calls
- In `start_custom_erd_polling_()`, pass `&this->erd_cache_` to `mqtt_bridge_polling_init_at_address()`

### 1.6 Simplify cache stats sensor reading

**`geappliances_bridge.cpp`**
- In `loop()`, replace the conditional cache pointer selection with direct use of `&this->erd_cache_`

### 1.7 Update tests

**`test/tests/mqtt_bridge_test.cpp`**
- Add `erd_cache_t test_cache;` to TEST_GROUP
- Initialize in setup: `erd_cache_init(&test_cache);`
- Destroy in teardown: `erd_cache_destroy(&test_cache);`
- Pass `&test_cache` to `mqtt_bridge_init()`

**`test/tests/mqtt_bridge_polling_test.cpp`**
- Add `erd_cache_t test_cache;` to TEST_GROUP
- Initialize/destroy same as above
- Pass `&test_cache` to `mqtt_bridge_polling_init()`
- Update any tests that reference `self.erd_cache` to use the shared cache

---

## Phase 2: Create ERD Cache MQTT Publisher Module

### 2.1 New files

**`components/geappliances_bridge/erd_cache_mqtt_publisher.h`**
```c
#ifndef erd_cache_mqtt_publisher_h
#define erd_cache_mqtt_publisher_h

#include <stdint.h>
#include <stdbool.h>

#include "erd_cache.h"
#include "i_mqtt_client.h"
#include "i_tiny_event.h"

typedef struct {
  erd_cache_t* cache;              // Shared cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;    // MQTT publish interface
  const char* device_id;           // Device ID string for topic construction
  uint16_t publish_index;          // Round-robin index into cache entries
  bool mqtt_connected;             // True when MQTT broker is connected
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t mqtt_connect_subscription;
  // Stats
  uint32_t total_published;        // Total ERD publishes since init
  uint32_t dropped_count;          // Publishes dropped due to MQTT disconnect
} erd_cache_mqtt_publisher_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_mqtt_publisher_init(
  erd_cache_mqtt_publisher_t* self,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client,
  const char* device_id);

void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self);

// Publish up to max_publishes ERDs within max_ms time budget.
// Returns the number of ERDs actually published.
// No-ops if MQTT is disconnected (increments dropped_count).
uint16_t erd_cache_mqtt_publisher_loop(
  erd_cache_mqtt_publisher_t* self,
  uint16_t max_publishes,
  uint32_t max_ms);

// Called when MQTT broker connects.
void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self);

// Called when MQTT broker disconnects.
void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self);

#ifdef __cplusplus
}
#endif

#endif
```

**`components/geappliances_bridge/erd_cache_mqtt_publisher.cpp`**
- `init()`: Zero the struct, store params, subscribe to `on_mqtt_disconnect` event from `mqtt_client`, set `mqtt_connected = true`
- `destroy()`: Unsubscribe events, zero struct
- `loop()`:
  - If `!mqtt_connected`, log warning once, return 0
  - Record `start_ms = millis()`
  - Loop up to `max_publishes` times:
    - Call `erd_cache_get_next_updated(self->cache, &self->publish_index)`
    - If NULL, break (no more updated entries)
    - If `millis() - start_ms >= max_ms`, break (time budget exceeded)
    - Build topic: `geappliances/{device_id}/erd/0x{ERD:04X}/value`
    - Build payload: uppercase hex string of data bytes, no separator, no prefix
    - Call `esphome_mqtt_client_adapter_publish()` with `retain=true`
    - Increment `total_published`
  - Return count published
- `on_connected()`: Set `mqtt_connected = true`, log info
- `on_disconnected()`: Set `mqtt_connected = false`, log warning

### 2.2 Wire publisher into GeappliancesBridge

**`geappliances_bridge.h`**
- Add `#include "erd_cache_mqtt_publisher.h"`
- Add `erd_cache_mqtt_publisher_t erd_cache_publisher_;` as protected member
- Add `void init_erd_cache_publisher_();` as protected method

**`geappliances_bridge.cpp`**
- In `teardown()`, call `erd_cache_mqtt_publisher_destroy(&this->erd_cache_publisher_);` before `Component::teardown()`
- In `loop()`, after the HSM `signal_run_loop` dispatch, add:
  ```cpp
  if (this->erd_cache_publisher_.cache != nullptr) {
    erd_cache_mqtt_publisher_loop(&this->erd_cache_publisher_, 20, 10);
  }
  ```

**`geappliances_bridge_bridge_init.cpp`**
- Add `init_erd_cache_publisher_()` method:
  ```cpp
  void GeappliancesBridge::init_erd_cache_publisher_() {
    if (this->erd_cache_publisher_.cache) return; // already init
    erd_cache_mqtt_publisher_init(
      &this->erd_cache_publisher_,
      &this->erd_cache_,
      &this->mqtt_client_adapter_.interface,
      this->device_identity_manager_.get_device_id().c_str());
    ESP_LOGI(TAG, "ERD cache MQTT publisher initialized");
  }
  ```

### 2.3 Add publisher init to startup HSM

**`geappliances_bridge_startup_hsm.h`**
- No new signal needed — we hook into existing `signal_device_id_complete`

**`geappliances_bridge_startup_hsm.cpp`**
- In `startup_state_device_id`, in the `signal_device_id_complete` handler, before transitioning to `startup_state_mqtt_client_init`, add no change — instead, modify `startup_state_mqtt_client_init` entry to also call the publisher init

**`i_bridge_services.h`**
- Add `virtual void initialize_erd_cache_publisher() = 0;`
- Add `virtual bool is_erd_cache_publisher_initialized() const = 0;`

**`geappliances_bridge.cpp`** (IBridgeServices impl)
- Implement `initialize_erd_cache_publisher()` → calls `init_erd_cache_publisher_()`
- Implement `is_erd_cache_publisher_initialized()` → returns `erd_cache_publisher_.cache != nullptr`

**`geappliances_bridge_bridge_init.cpp`**
- In `startup_state_mqtt_client_init`, after `svc->initialize_mqtt_client()`, add:
  ```cpp
  svc->initialize_erd_cache_publisher();
  ```
  This ensures the publisher is initialized right after the MQTT client adapter, once the device ID is available.


---

## Phase 3: Hook MQTT disconnect/reconnect events

### 3.1 Wire disconnect subscription in publisher

**`erd_cache_mqtt_publisher.cpp`**
- In `init()`, subscribe to `mqtt_client_on_mqtt_disconnect(self->mqtt_client)`:
  ```c
  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self,
    +[](void* context, const void*) {
      erd_cache_mqtt_publisher_on_disconnected(
        reinterpret_cast<erd_cache_mqtt_publisher_t*>(context));
    });
  tiny_event_subscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);
  ```
- In `destroy()`, unsubscribe from the disconnect event

### 3.2 Notify-connected callback

The `esphome_mqtt_client_adapter_t` already has `notify_connected()` and `notify_disconnected()` methods. The publisher needs to hook into these.

**Option A (recommended)**: Add a callback field to `erd_cache_mqtt_publisher_t` and have `GeappliancesBridge` call it when the adapter notifies connection state changes.

**Option B**: Have the publisher subscribe to the `on_mqtt_disconnect` event from `i_mqtt_client_t` (which already exists). For reconnect, the adapter already calls `notify_connected()` — we need a symmetric event.

**Decision**: Use Option B. The `i_mqtt_client_t` interface already has `on_mqtt_disconnect`. We'll add `on_mqtt_connect` to the interface for symmetry.

**`i_mqtt_client.h`**
- Add `i_tiny_event_t* (*on_mqtt_connect)(i_mqtt_client_t* self);` to the API struct
- Add static inline wrapper `mqtt_client_on_mqtt_connect()`

**`esphome_mqtt_client_adapter.h`**
- Add `tiny_event_t on_mqtt_connect_event;` to the adapter struct

**`esphome_mqtt_client_adapter.cpp`**
- Initialize `on_mqtt_connect_event` in `init()`
- Add `on_mqtt_connect` to the API struct
- In `notify_connected()`, publish the `on_mqtt_connect_event`

**`erd_cache_mqtt_publisher.cpp`**
- In `init()`, subscribe to `mqtt_client_on_mqtt_connect()` event
- In `destroy()`, unsubscribe
- Wire `on_connected` callback


---

## Phase 4: Tests

### 4.1 Unit tests for erd_cache_mqtt_publisher

**New file: `test/tests/erd_cache_mqtt_publisher_test.cpp`**

Test cases:
1. `init_sets_cache_pointer` - verifies struct fields after init
2. `loop_publishes_updated_erd` - cache has one updated entry, loop publishes it, verifies topic format and hex payload
3. `loop_respects_max_publishes` - cache has 50 updated entries, max_publishes=10, only 10 published
4. `loop_respects_time_budget` - cache has 100 updated entries, max_ms=1, stops within budget
5. `loop_round_robin_index` - publish batch of 5, then another batch of 5, verify different ERDs published (index advances)
6. `loop_returns_zero_when_no_updates` - cache has no update_required entries, returns 0
7. `loop_skips_when_mqtt_disconnected` - simulate disconnect, verify no publishes, dropped_count increases
8. `loop_resumes_after_reconnect` - disconnect, then reconnect, verify publishes resume
9. `topic_format_correct` - verify topic matches `geappliances/{deviceId}/erd/0x0008/value`
10. `payload_uppercase_hex_no_separator` - verify `{0x01, 0xAB, 0xFF}` → `"01ABFF"`
11. `retain_flag_true` - verify retain=true is passed to publish
12. `on_disconnected_sets_flag` - verify mqtt_connected becomes false
13. `on_connected_sets_flag` - verify mqtt_connected becomes true
14. `destroy_unsubscribes_events` - verify no use-after-free on destroy

### 4.2 Update existing tests

**`test/tests/mqtt_bridge_test.cpp`**
- Add `erd_cache_t test_cache;` member to TEST_GROUP
- Init/destroy in setup/teardown
- Pass `&test_cache` to `mqtt_bridge_init()`

**`test/tests/mqtt_bridge_polling_test.cpp`**
- Add `erd_cache_t test_cache;` member to TEST_GROUP
- Init/destroy in setup/teardown
- Pass `&test_cache` to `mqtt_bridge_polling_init()`
- Update any assertions that reference `self.erd_cache` to use `test_cache`

### 4.3 Update Makefile

**`Makefile`**
- Add `components/geappliances_bridge/erd_cache_mqtt_publisher.cpp` to `SRC_FILES`
- Add `test/tests/erd_cache_mqtt_publisher_test.cpp` is picked up automatically by the `find` in `SRC_DIRS`


---

## Phase 5: Verification

### 5.1 Build and test

```bash
make test
```

Verify:
- All existing tests pass (mqtt_bridge_test, mqtt_bridge_polling_test, esphome_mqtt_client_adapter_test)
- New erd_cache_mqtt_publisher_test passes
- No compiler warnings

### 5.2 ESPHome compilation check

```bash
esphome compile config.yaml
```

Verify the component compiles in the ESPHome environment.

---

## Summary of File Changes

| File | Change |
|------|--------|
| `erd_cache.h` | No changes |
| `erd_cache.cpp` | No changes |
| `mqtt_bridge.h` | `erd_cache_t erd_cache` → `erd_cache_t* erd_cache` |
| `mqtt_bridge.cpp` | Accept cache pointer in init, remove init/destroy calls |
| `mqtt_bridge_polling.h` | `erd_cache_t erd_cache` → `erd_cache_t* erd_cache` |
| `mqtt_bridge_polling.cpp` | Accept cache pointer in init, remove init/destroy calls |
| `geappliances_bridge.h` | Add `erd_cache_` member, `erd_cache_publisher_` member, new methods |
| `geappliances_bridge.cpp` | Init/destroy cache, call publisher loop(), IBridgeServices impl |
| `geappliances_bridge_bridge_init.cpp` | Wire cache pointer to bridges, init publisher after device ID |
| `geappliances_bridge_startup_hsm.h` | No changes |
| `geappliances_bridge_startup_hsm.cpp` | No changes (publisher init called from mqtt_client_init phase) |
| `i_bridge_services.h` | Add `initialize_erd_cache_publisher()`, `is_erd_cache_publisher_initialized()` |
| `i_mqtt_client.h` | Add `on_mqtt_connect` to API struct + inline wrapper |
| `esphome_mqtt_client_adapter.h` | Add `on_mqtt_connect_event` field |
| `esphome_mqtt_client_adapter.cpp` | Init connect event, publish on notify_connected, add to API |
| `erd_cache_mqtt_publisher.h` | **NEW** - Publisher struct and API |
| `erd_cache_mqtt_publisher.cpp` | **NEW** - Publisher implementation |
| `erd_cache_mqtt_publisher_test.cpp` | **NEW** - Unit tests |
| `mqtt_bridge_test.cpp` | Pass cache pointer to init |
| `mqtt_bridge_polling_test.cpp` | Pass cache pointer to init |
| `Makefile` | Add new source file to SRC_FILES |
