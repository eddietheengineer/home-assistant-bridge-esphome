# Rearchitecture Plan: Full Decoupling of `geappliances_bridge`

This document describes all phases required to complete the architectural
decoupling of the `geappliances_bridge` component.  It is intended as a
living checklist: check off a phase once all acceptance criteria are met and
all related tests pass.

---

## Background and Goals

The original `loop()` was a monolithic MQTT FSM + protocol driver + HSM tick
with no shared message bus between modules.  The rearchitecture extracts each
concern into a self-contained class that:

- Owns its own state
- Communicates through the `ErdDataBus` (publish/subscribe) or discrete events
- Is driven by a `tiny_timer_t` instead of a `signal_run_loop` polling call

The target steady-state `loop()` body is:

```cpp
void GeappliancesBridge::loop() {
  run_protocol_stack_();          // hardware RX/TX
  esp_task_wdt_reset();
}
```

Everything else — MQTT FSM, bridge draining, subscription watchdog, HA
discovery, custom ERD polling — runs via timers registered in
`timer_group_` and communicates via `ErdDataBus` subscriptions.

---

## Phase 1 — ErdDataBus, MqttConnectionManager, decoupled loop  ✅ DONE

**PR:** _Rearchitecture: ErdDataBus, MqttConnectionManager, decoupled main loop_

### What changed

| Area | Change |
|------|--------|
| `erd_data_bus.h/.cpp` | New: central ERD state table. Maps `tiny_erd_t` → raw bytes + dirty flag + per-ERD `tiny_event_t` subscription. |
| `erd_data_bus_ids.h` | New: internal ERD namespace `0xF000–0xFFFF` for cross-module signalling. `ERD_INTERNAL_MQTT_STATE = 0xF000`. |
| `mqtt_connection_manager.h/.cpp` | New: extracts the 4-state MQTT FSM from `loop()` into a 100 ms periodic timer. Fires `on_connected()` / `on_disconnected()` events; writes `ERD_INTERNAL_MQTT_STATE` to the bus. |
| `geappliances_bridge.cpp` | HSM `init` moved from `loop()` → `setup()`. `loop()` reduced to `run_protocol_stack_()` + watchdog reset + `signal_run_loop`. |
| `geappliances_bridge.cpp` | `MqttConnectionManager::init()` called from `setup()`, timer period set to 100 ms. |

### Acceptance criteria

- [x] All unit tests pass (`make test`)
- [x] Device flashes and reaches `RUNNING` state
- [x] ERD 0x0002 (device generation) reads successfully at 100 ms timer cadence

---

## Phase 2 — Migrate `pending_updates` map to `ErdDataBus`

### Problem

`esphome_mqtt_client_adapter_t` owns a `std::map<tiny_erd_t, PendingErdUpdate>*
pending_updates` allocated on the heap.  This map duplicates storage that
`ErdDataBus` is now designed to provide:

- The map holds the raw bytes of every queued ERD update.
- `ErdDataBus` already stores raw bytes per ERD with a dirty flag.
- `ErdDataBus::mark_all_dirty()` / `for_each_dirty()` / `clear_dirty()` were
  designed precisely for reconnect-flush semantics.

Keeping both structures means two allocations, two serialisation paths, and two
places where "which ERD value is most recent" can diverge.

### What needs to change

#### 2a — Remove `pending_updates` from `esphome_mqtt_client_adapter_t`

**File:** `esphome_mqtt_client_adapter.h`

- Remove the `std::map<tiny_erd_t, PendingErdUpdate>* pending_updates` member
  from the C struct.
- Remove the `PendingErdUpdate` struct (no longer needed).
- Remove `esphome_mqtt_client_adapter_drain_pending_updates()` from the public
  API (its callers move to `ErdDataBus::for_each_dirty()`).
- Remove `esphome_mqtt_client_adapter_get_pending_update_count()`.

#### 2b — Write ERD updates into `ErdDataBus` instead of `pending_updates`

**File:** `esphome_mqtt_client_adapter.cpp`, function `update_erd()`

- Change `update_erd()` to write the formatted `(topic, payload)` pair into
  an `ErdDataBus` entry instead of `(*self->pending_updates)[erd]`.
- The adapter will need a pointer to the shared `ErdDataBus` (pass it through
  the existing `esphome_mqtt_client_adapter_init()` signature, or via a new
  `esphome_mqtt_client_adapter_set_erd_data_bus()` setter to keep the C ABI
  stable).

**Payload storage option:** Store the formatted hex/string payload as a
`std::string` inside the `ErdDataBus` entry's `data` vector (cast to/from
`uint8_t*`).  Alternatively, keep the per-ERD `topic` as a second parallel
store; the simplest approach is a small helper struct:

```cpp
struct ErdMqttPayload {
  std::string topic;
  std::string payload;
};
```

stored via `bus_->write(erd, &payload_struct, sizeof(ErdMqttPayload))` using a
placement-new or a dedicated string-map overlay on top of `ErdDataBus`.

> **Note:** Because `ErdDataBus` currently stores raw `uint8_t` blobs,
> the cleanest approach may be a separate `ErdMqttPublishBus` class that
> maps `tiny_erd_t` → `{topic, payload}` strings, keeping the raw-bytes
> bus for hardware ERD values and the publish bus for formatted MQTT payloads.
> Decide which approach to take before starting 2b.

#### 2c — Replace `drain_pending_updates()` calls in `MqttConnectionManager`

**File:** `mqtt_connection_manager.cpp`

The FSM states `FLUSHING` and `RUNNING` currently call
`esphome_mqtt_client_adapter_drain_pending_updates(adapter_)`.  Replace with:

```cpp
// In FLUSHING:
size_t remaining = 0;
bus_->for_each_dirty([&](tiny_erd_t erd, const void* data, uint8_t size) {
  // publish from data, then:
  bus_->clear_dirty(erd);
  remaining = bus_->dirty_count();
});
if (remaining == 0) { transition to RUNNING; }

// In RUNNING:
bus_->for_each_dirty([&](tiny_erd_t erd, const void* data, uint8_t size) {
  // publish, then clear_dirty
});
```

#### 2d — Mark all ERDs dirty on MQTT reconnect

**File:** `mqtt_connection_manager.cpp` (transition DISCONNECTED → SUBSCRIBING)

```cpp
// When MQTT reconnects:
if (bus_ != nullptr) {
  bus_->mark_all_dirty();
}
```

This replaces the legacy behaviour in `esphome_mqtt_client_adapter_notify_connected()`
where the adapter called `drain_pending_updates()` directly.

#### 2e — Remove legacy `notify_connected()` / `notify_disconnected()` path

**File:** `esphome_mqtt_client_adapter.cpp`

- `esphome_mqtt_client_adapter_notify_connected()` currently calls
  `subscribe_write_topic()` and `drain_pending_updates()`.  After phase 2,
  draining is handled by `MqttConnectionManager`; remove the drain call.
- `esphome_mqtt_client_adapter_notify_disconnected()` resets
  `mqtt_connected_at_ms` and fires the disconnect event.  This is still needed
  for the subscription/polling bridges — keep but ensure the new drain path is
  no longer called from here.

#### 2f — Update unit tests

**File:** `test/tests/esphome_mqtt_client_adapter_test.cpp`,
`test/tests/mqtt_connection_manager_test.cpp`

- Remove tests that exercise `pending_updates` map internals.
- Add tests that verify `ErdDataBus` dirty entries are flushed in `FLUSHING`
  state and that `mark_all_dirty()` is called on reconnect.

### Acceptance criteria

- [ ] `esphome_mqtt_client_adapter_t` no longer owns a heap-allocated map
- [ ] ERD values published to MQTT survive a broker disconnect/reconnect cycle
  without duplicates or data loss
- [ ] `make test` passes with no regressions

---

## Phase 3 — Per-module timers (eliminate remaining `signal_run_loop` polling)

### Problem

`loop()` still sends `signal_run_loop` to the startup HSM on every iteration.
Each HSM state's `signal_run_loop` handler polls a manager's completion flag
(e.g., `is_autodiscovery_complete()`, `is_device_id_complete()`).  This is
pull-based polling rather than event-driven notification.

The remaining polling sites inside the HSM `signal_run_loop` handlers are:

| HSM state | Polling call |
|-----------|--------------|
| `startup_state_startup_delay` | `is_startup_delay_elapsed()` (millis compare) |
| `startup_state_autodiscovery` | `is_autodiscovery_complete()` |
| `startup_state_device_id` | `is_device_id_complete()` |
| `startup_state_feature_bits` | `is_feature_bits_complete()` |
| `startup_state_subscription_watch` | `check_subscription_activity()` |
| `startup_state_ha_discovery` | (HA discovery manager driven by FreeRTOS task — already async) |
| `startup_state_running` | `run_all_managers()` (drains MQTT, checks subscription) |

### What needs to change

#### 3a — Replace startup delay with a one-shot timer

**File:** `geappliances_bridge_startup_hsm.cpp` (state `startup_state_startup_delay`)

Replace the `is_startup_delay_elapsed()` millis poll with a `tiny_timer_t`
one-shot started in the state's `tiny_hsm_signal_entry` handler:

```cpp
case tiny_hsm_signal_entry:
  tiny_timer_start(timer_group, &delay_timer, AUTODISCOVERY_STARTUP_DELAY_MS,
                   context, [](void* ctx) {
                     auto* hsm = static_cast<tiny_hsm_t*>(ctx);
                     tiny_hsm_send_signal(hsm, signal_startup_delay_elapsed, nullptr);
                   });
  break;
case signal_startup_delay_elapsed:
  tiny_hsm_transition(hsm, startup_state_autodiscovery);
  break;
```

Remove `record_startup_delay_start()`, `is_startup_delay_elapsed()`, and
`startup_delay_start_ms_` from `IBridgeServices` and `GeappliancesBridge`.

#### 3b — Replace autodiscovery completion poll with a callback

`AutodiscoveryManager` already calls the completion callback lambda passed in
`init()`.  The bridge currently still calls `is_autodiscovery_complete()` from
the HSM `signal_run_loop` handler as a belt-and-suspenders check.

- Remove the `signal_run_loop` poll from `startup_state_autodiscovery`.
- The existing lambda in `geappliances_bridge.cpp` already fires
  `signal_autodiscovery_complete` — that is sufficient.
- Remove `is_autodiscovery_complete()` from `IBridgeServices` once the poll
  is gone.

#### 3c — Replace device ID completion poll with an ERD bus event

`DeviceIdentityManager::on_erd_read_completed()` is called by the bridge's
`handle_erd_client_activity_()` callback and already fires
`signal_device_id_complete` when complete.

- Remove the `signal_run_loop` polling branch from `startup_state_device_id`.
- Remove `is_device_id_complete()` from `IBridgeServices` once the poll
  is gone.

#### 3d — Replace feature bit completion poll with a callback

`FeatureBitManager` does not currently have a completion callback.  Add one:

```cpp
void FeatureBitManager::set_on_complete(std::function<void()> cb);
```

Call it in `mark_complete()` / `mark_timed_out()`.  Register it in `setup()`
to fire `signal_feature_bits_complete`.

Remove the `signal_run_loop` poll from `startup_state_feature_bits` and
remove `is_feature_bits_complete()` from `IBridgeServices`.

#### 3e — Replace subscription watchdog poll with a timer

**File:** `geappliances_bridge_startup_hsm.cpp` (state `startup_state_subscription_watch`)

Start a `SUBSCRIPTION_TIMEOUT_MS` one-shot timer on entry.  On expiry, fire
`signal_subscription_fallback`.  Activity resets the timer via a new
`i_bridge_services::reset_subscription_watchdog()` method, or by cancelling and
restarting the timer directly.

Remove `check_subscription_activity()` from `IBridgeServices`.

#### 3f — Replace `run_all_managers()` steady-state drain with a timer

**File:** `geappliances_bridge_startup_hsm.cpp` (state `startup_state_running`)

The `run_all_managers()` call in `signal_run_loop` currently drains pending
MQTT updates and checks subscription activity.  After phases 2 and 3e, this
reduces to nothing (draining is timer-driven via `MqttConnectionManager`).

Remove `run_all_managers()` from `IBridgeServices` and the HSM
`signal_run_loop` handler.

#### 3g — Remove `signal_run_loop` from `loop()`

Once all states have migrated away from `signal_run_loop`, remove the
`tiny_hsm_send_signal(&startup_hsm_, signal_run_loop, nullptr)` call from
`loop()`.

**Final `loop()` body:**

```cpp
void GeappliancesBridge::loop() {
  run_protocol_stack_();
  esp_task_wdt_reset();
}
```

#### 3h — Remove IBridgeServices methods that are no longer needed

After 3a–3g the following `IBridgeServices` pure-virtual methods can be
deleted:

- `is_autodiscovery_complete()`
- `is_device_id_complete()`
- `record_startup_delay_start()` / `is_startup_delay_elapsed()`
- `is_feature_bits_complete()`
- `check_subscription_activity()`
- `run_all_managers()`
- `log_poll_state_transitions()` (already only called from `signal_run_loop`)

### Acceptance criteria

- [ ] `loop()` body is `run_protocol_stack_()` + watchdog reset only
- [ ] All startup phases still complete in the correct order end-to-end
- [ ] No regressions in `make test`

---

## Phase 4 — Remove legacy member duplication from `GeappliancesBridge`

### Problem (Iteration log issue 8)

`GeappliancesBridge` holds parallel copies of state already owned by managers:

| Bridge member | True owner |
|---------------|-----------|
| `configured_device_id_` | `DeviceIdentityManager` |
| `subscription_mode_active_` | could live in `AutodiscoveryManager` or a new `BridgeModeManager` |
| `subscription_activity_detected_` | belongs entirely in the subscription watchdog (phase 3e) |
| `subscription_start_time_` | belongs in the subscription watchdog timer |
| `custom_erd_subscription_last_activity_` | belongs in the HA discovery manager |
| `custom_erd_subscription_seen_erds_` | belongs in `HaDiscoveryManager` |
| `custom_erd_polling_started_` | belongs in `MqttBridgePolling` init guard |
| `feature_bit_reading_started_` | belongs in `FeatureBitManager` |
| `gea2_protocol_active_` | redundant — `AutodiscoveryManager::is_gea2_protocol()` is the canonical source |
| `last_logged_poll_state_` | belongs in the polling bridge or a debug helper |
| `startup_delay_start_ms_` | removed in phase 3a |

### What needs to change

#### 4a — Move mode tracking into `AutodiscoveryManager` or `BridgeModeManager`

`subscription_mode_active_`, `subscription_activity_detected_`, and
`subscription_start_time_` all track "did subscription mode work?" which is the
responsibility of the subscription watchdog (phase 3e).  After phase 3e, these
members are no longer needed.

#### 4b — Move activity tracking into `HaDiscoveryManager`

`custom_erd_subscription_seen_erds_` and `custom_erd_subscription_last_activity_`
are only read by `ha_discovery_manager_.on_erd_seen()` and the quiet-window
timer.  Move them into `HaDiscoveryManager` as private state; expose a single
`on_erd_activity(tiny_erd_t erd)` method.

#### 4c — Eliminate `custom_erd_polling_started_` and `feature_bit_reading_started_`

Both are init guards on one-time operations.  After phases 2–3, both
operations are triggered by timer callbacks; the guard becomes a boolean inside
the callback closure or inside the manager itself.  Remove from the bridge.

#### 4d — Remove `gea2_protocol_active_` from bridge

Replace the two usage sites with `autodiscovery_manager_.is_gea2_protocol()`.

#### 4e — Remove `configured_device_id_` from the bridge after setup

`configured_device_id_` is used only to initialize `DeviceIdentityManager` in
`init_device_id_reading()`.  Pass it at construction time; the bridge does not
need to retain it thereafter.

#### 4f — Remove `last_logged_poll_state_`

Move polling bridge state transition logging into `MqttBridgePolling` itself
(it already holds `current_state_name`), or into a timer callback inside the
polling bridge.

### Acceptance criteria

- [ ] `geappliances_bridge.h` member list contains only: hardware peripherals
  (`uart_`, buffers, interfaces), managers, the timer group, and the bus
- [ ] No manager has a "legacy sync" method called from the bridge
- [ ] No regressions in `make test`

---

## Phase 5 — Unit test coverage for startup HSM timeout paths

### Problem (Iteration log issue 10)

The feature-bits timeout path (fixed in Iteration 1, issue 1) and the device-id
timeout path have no unit tests.

### What needs to change

**File:** `test/tests/startup_hsm_test.cpp`

Add the following test groups:

#### 5a — Feature bit timeout

```
TEST(StartupHsm, TransitionsToDeviceIdAfterFeatureBitTimeout)
  - Advance the HSM to startup_state_feature_bits
  - Do NOT fire signal_feature_bits_complete
  - Advance millis() past the feature_bits phase timeout
  - Send signal_run_loop (or wait for timer after phase 3d)
  - Assert: HSM is in startup_state_bridge_init (or mqtt_client_init, depending on timeout target)
  - Assert: FeatureBitManager.is_complete() == true
```

#### 5b — Device ID timeout

```
TEST(StartupHsm, TransitionsToMqttClientInitAfterDeviceIdTimeout)
  - Advance the HSM to startup_state_device_id
  - Do NOT fire signal_device_id_complete
  - Advance millis() past the device_id phase timeout
  - Assert: HSM is in startup_state_mqtt_client_init
```

#### 5c — Autodiscovery timeout

```
TEST(StartupHsm, TransitionsToDeviceIdAfterAutodiscoveryTimeout)
  - Start in startup_state_autodiscovery
  - Simulate timeout expiry
  - Assert: HSM transitions to startup_state_device_id
  - Assert: discovered_host_address defaults to default appliance address
```

#### 5d — MQTT connect timeout

```
TEST(StartupHsm, TransitionsToWaitingAfterMqttTimeout)
  - Start in startup_state_mqtt_client_init
  - Do not fire signal_mqtt_connected
  - Advance past timeout
  - Assert: HSM stays in startup_state_mqtt_client_init (no crash, retries)
```

#### 5e — Subscription fallback (AUTO mode)

```
TEST(StartupHsm, FallsBackToPollingWhenSubscriptionTimesOut)
  - Bridge in AUTO mode, HSM in startup_state_subscription_watch
  - Do not deliver any subscription publications
  - Advance past SUBSCRIPTION_TIMEOUT_MS
  - Assert: signal_subscription_fallback was sent
  - Assert: polling bridge is initialized
```

### Acceptance criteria

- [ ] All new tests compile against the mock `IBridgeServices` harness
- [ ] All new tests pass: `make test`
- [ ] Code coverage for `geappliances_bridge_startup_hsm.cpp` timeout branches ≥ 80 %

---

## Phase 6 — Update module documentation

### Problem (Iteration log issue 9)

The `doc/module_descriptions/` directory is missing entries for:

- `ErdDataBus`
- `MqttConnectionManager`
- `ErdRegistry` (extracted in Iteration 1 but not yet documented)

And existing entries are stale for modules changed in Iteration 1 and Phase 1:

- `geappliances_bridge.md` — `loop()` description and dependency list need
  updating after phases 1–3.
- `geappliances_bridge_startup_hsm.md` — needs timeout path documentation.
- `esphome_mqtt_client_adapter.md` — needs updating after phase 2.

### What needs to change

#### 6a — New: `doc/module_descriptions/erd_data_bus.md`

Sections: Purpose, Public API table, Memory model, Internal ERD ID range,
Usage examples, Dependencies.

#### 6b — New: `doc/module_descriptions/mqtt_connection_manager.md`

Sections: Purpose, State diagram (ASCII), Timer period and rationale, Public
API table, Integration with `ErdDataBus`, Dependencies.

#### 6c — New: `doc/module_descriptions/erd_registry.md`

Sections: Purpose, Public API table, How it interacts with `FeatureBitManager`
and `EsphomeMqttClientAdapter`, Dependencies.

#### 6d — Update `geappliances_bridge.md`

- Remove references to the MQTT FSM (now in `MqttConnectionManager`).
- Update `loop()` description to reflect phase 3 final state.
- Add `ErdDataBus` and `MqttConnectionManager` to the dependency list.
- Remove methods removed in phases 3–4.

#### 6e — Update `geappliances_bridge_startup_hsm.md`

- Add timeout guard table: which states have timeouts, their duration, and
  what state they transition to on expiry.
- Document the new timer-driven transitions from phase 3.

#### 6f — Update `esphome_mqtt_client_adapter.md`

- Remove `pending_updates` map from the Public API table.
- Add `ErdDataBus` to the dependency list.
- Document the new drain path via `MqttConnectionManager`.

### Acceptance criteria

- [ ] Each new `.md` file covers Purpose, Public API, and Dependencies
- [ ] No module description references a method that was removed in phases 1–4
- [ ] `doc/iteration_log.md` issue 9 is closed

---

## Phase 7 — Close remaining `iteration_log.md` items

These are smaller follow-ups captured in `doc/iteration_log.md`.

### Issue 8 — Legacy member duplication (covered by phase 4 above)

### Issue 9 — Module documentation (covered by phase 6 above)

### Issue 10 — Tests for startup HSM timeout paths (covered by phase 5 above)

---

## Dependency graph between phases

```
Phase 1 (DONE)
    │
    ├─▶ Phase 2  (ErdDataBus replaces pending_updates)
    │       │
    │       └─▶ Phase 3  (per-module timers, eliminate signal_run_loop)
    │               │
    │               └─▶ Phase 4  (remove legacy bridge members)
    │
    ├─▶ Phase 5  (unit tests — can start immediately, no code dependency)
    │
    └─▶ Phase 6  (docs — can start immediately, should be updated as code lands)
```

Phases 5 and 6 can be worked on in parallel with phases 2–4.

---

## Definition of "rearchitecture complete"

The rearchitecture is complete when all of the following are true:

1. `loop()` contains only `run_protocol_stack_()` and `esp_task_wdt_reset()`.
2. `ErdDataBus` is the sole source of truth for all in-flight ERD values.
3. `IBridgeServices` has no polling methods — only one-time action triggers.
4. `GeappliancesBridge` holds only hardware peripherals, managers, and the
   timer group (no duplicated state).
5. All startup HSM timeout branches have unit test coverage.
6. All `doc/module_descriptions/` files are accurate and complete.
