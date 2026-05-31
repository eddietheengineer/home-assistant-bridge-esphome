# Phase 4: Integration & Orchestration

## Purpose

Wire the new modules into `GeappliancesBridge` and update `StartupHsm` to populate `GlobalStateRegistry`. After this phase, the bridge runs entirely through the new FSMs and the old C bridges are deleted.

---

## Prerequisites (Phases 1–3 complete)

- `GlobalStateRegistry` and `ErdStateTable` (Phase 1)
- `WriteQueue` (Phase 1)
- `SubscriptionHandler`, `PollingHandler`, `WriteHandler`, `ApplianceSideStateMachine` (Phase 2)
- `WriteRouter`, `MqttSideStateMachine` (Phase 3)
- `mqtt_bridge.c` and `mqtt_bridge_polling.c` — still present; deleted in this phase

---

## Key Design Decisions for This Phase

### `run_protocol_stack_()` stays first in `GeappliancesBridge::loop()`
`run_protocol_stack_()` runs a 200 ms wall-clock busy loop for GEA2 hardware timing. It must remain at the top of `GeappliancesBridge::loop()`, **before** both FSM `loop()` calls. Never move this into any state machine.

Correct `loop()` structure:
```cpp
void GeappliancesBridge::loop() {
  run_protocol_stack_();    // 1. GEA2/GEA3 protocol — must be first
  appliance_fsm_->loop();   // 2. Appliance-side FSM
  mqtt_fsm_->loop();        // 3. MQTT-side FSM
}
```

### WriteRouter and MqttSideStateMachine are constructed inside `initialize_mqtt_client_()`
`esphome_mqtt_client_adapter_init()` is NOT called in `setup()`. It is called inside `initialize_mqtt_client_()`, which fires from the startup HSM after device ID is ready. `WriteRouter` subscribes to `on_write_request_event` at construction; that event is only valid after `esphome_mqtt_client_adapter_init()` runs.

Therefore `write_router_` and `mqtt_side_state_machine_` must be `std::unique_ptr` members constructed at the **end of `initialize_mqtt_client_()`** (or in a new `initialize_mqtt_fsm_()` helper called from there). Do NOT construct them in `setup()`.

### Handler selection is mode-conditional
Construct only the handlers needed based on `mode_`:
- `BRIDGE_MODE_SUBSCRIBE`: `SubscriptionHandler` + `WriteHandler` only
- `BRIDGE_MODE_POLL`: `PollingHandler` + `WriteHandler` only
- `BRIDGE_MODE_AUTO`: all three (`SubscriptionHandler` first, `PollingHandler` as fallback, `WriteHandler` always)

Do not construct unused handlers. Null-handler guards in the FSM would be fragile.

### `ErdRegistry*` is passed from `GeappliancesBridge`
`erd_registry_` is a direct member of `GeappliancesBridge`. Pass `&this->erd_registry_` to `MqttSideStateMachine`. The bridge owns it; the FSM holds a raw non-owning pointer.

### The inline `MqttConnectionState` FSM switch block is removed
`GeappliancesBridge::loop()` currently contains an inline `switch(mqtt_connection_state_)` block. After `MqttSideStateMachine` is wired in, this block is deleted. `MqttSideStateMachine::loop()` replaces it entirely.

### `signal_mqtt_connected` stays in the connect callback; FSM is also notified there
After removing the inline switch, the existing `on_connect` / `on_disconnect` lambdas in `loop()` keep firing. Update their bodies to drive both the startup HSM and the new FSM:
```cpp
// on_connect:
tiny_hsm_send_signal(&this->startup_hsm_, signal_mqtt_connected, nullptr); // unchanged
if (mqtt_fsm_) mqtt_fsm_->on_mqtt_connected();  // new

// on_disconnect:
esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_); // unchanged
if (mqtt_fsm_) mqtt_fsm_->on_mqtt_disconnected(); // new
```
The `if (mqtt_fsm_)` guard handles the window before `initialize_mqtt_client_()` has run.

### `handle_erd_client_activity_()` is kept — it is not a no-op
This method still serves three purposes after Phase 4:
1. **HA discovery** — routes subscription publications → `on_ha_discovery_erd_seen_()` → `ha_discovery_manager_.on_erd_seen()`
2. **AUTO mode watchdog** — sets `subscription_activity_detected_` flag
3. **Pre-bridge device-ID reads** — routes `read_completed`/`read_failed` → `device_identity_manager_`

What disappears in Phase 4: the routing to `mqtt_bridge_` / `mqtt_bridge_polling_` (those objects are gone). The rest of the method stays untouched. HaDiscoveryManager migration to `ErdStateTable` events is a future todo.

### Adapter `pending_updates` is removed in this phase
Once `MqttSideStateMachine` reads from `ErdStateTable` and calls `esphome_mqtt_client_adapter_publish()` directly, the following adapter members/functions become dead code and must be removed:
- `PendingErdUpdate` struct
- `pending_updates` map member (+ its init in `adapter_init`, destroy in `adapter_destroy`)
- `MAX_PENDING_UPDATES` constant
- `esphome_mqtt_client_adapter_drain_pending_updates()` — public API + implementation
- `esphome_mqtt_client_adapter_notify_connected()` — was subscribe + drain; FSM now calls `subscribe_write_topic()` directly
- `mqtt_connected_at_ms` field — was only used to gate the drain timing

Keep: `subscribe_write_topic()`, `is_connected()`, `publish()`, `notify_disconnected()`.

### `mqtt_bridge_common.h` is deleted alongside the bridges
This header only `#include`s `mqtt_bridge.h` and `mqtt_bridge_polling.h` and declares shared templates. It becomes dead code the moment both bridges are deleted. `subscription_handler.h` has a comment noting its origin but does not `#include` it — no dependency.

### `g_bridge_services` static fix is deferred to a separate change
The `static IBridgeServices* g_bridge_services` in `geappliances_bridge_startup_hsm.cpp` is a pure refactor (move pointer into context struct; change every state handler to use `ctx->bridge_services`). This has no behavior change and affects many lines in startup_hsm.cpp. Bundling it with the FSM cutover increases blast radius unnecessarily. Do it as a **separate commit/PR after Phase 4 is verified working**.

---

## Todos

### Todo 4.1: Update `GeappliancesBridge` for new FSMs
**ID:** `bridge-orchestration`
**Depends on:** `appliance-fsm`, `mqtt-fsm`
**Output:** Updated `geappliances_bridge.cpp`, `geappliances_bridge.h`, `geappliances_bridge_bridge_init.cpp`

**In `geappliances_bridge.h`:**
- Add `std::unique_ptr<ApplianceSideStateMachine> appliance_fsm_`
- Add `std::unique_ptr<WriteRouter> write_router_`
- Add `std::unique_ptr<MqttSideStateMachine> mqtt_fsm_`
- Add `std::unique_ptr<ErdStateTable> erd_state_table_`
- Add `std::unique_ptr<GlobalStateRegistry> global_registry_`
- Add `std::unique_ptr<WriteQueue> write_queue_`
- Add handler unique_ptrs as needed (`subscription_handler_`, `polling_handler_`, `write_handler_`)
- Remove `MqttConnectionState` enum and `mqtt_connection_state_` member (handled by `mqtt_fsm_`)
- Remove `mqtt_bridge_t mqtt_bridge_` and `mqtt_bridge_polling_t mqtt_bridge_polling_` members
- Remove `#include "mqtt_bridge.h"` and `#include "mqtt_bridge_polling.h"`

**In `geappliances_bridge.cpp` `loop()`:**
```cpp
void GeappliancesBridge::loop() {
  run_protocol_stack_();          // must remain first
  tiny_hsm_send_signal(&startup_hsm_, signal_run_loop, nullptr);
  if (appliance_fsm_) appliance_fsm_->loop();
  if (mqtt_fsm_) mqtt_fsm_->loop();
}
```
Remove the inline `switch(mqtt_connection_state_)` block entirely.

Update connect/disconnect callback bodies (see "signal_mqtt_connected" design decision above).

**In `initialize_mqtt_client_()`** (in `geappliances_bridge_bridge_init.cpp`):
After the existing `esphome_mqtt_client_adapter_init()` call, add:
```cpp
// Construct FSMs now that adapter is initialized
global_registry_ = std::make_unique<GlobalStateRegistry>();
global_registry_->set_device_id(this->device_identity_manager_.get_device_id());
// ... set other registry fields

erd_state_table_ = std::make_unique<ErdStateTable>();
write_queue_ = std::make_unique<WriteQueue>();

// Construct mode-appropriate handlers
if (mode_ == BRIDGE_MODE_SUBSCRIBE || mode_ == BRIDGE_MODE_AUTO) {
  subscription_handler_ = std::make_unique<SubscriptionHandler>(...);
}
if (mode_ == BRIDGE_MODE_POLL || mode_ == BRIDGE_MODE_AUTO) {
  polling_handler_ = std::make_unique<PollingHandler>(...);
}
write_handler_ = std::make_unique<WriteHandler>(...);
appliance_fsm_ = std::make_unique<ApplianceSideStateMachine>(...);

write_router_ = std::make_unique<WriteRouter>(&mqtt_client_adapter_.interface, write_queue_.get());
mqtt_fsm_ = std::make_unique<MqttSideStateMachine>(
  erd_state_table_.get(), global_registry_.get(), &erd_registry_,
  &mqtt_client_adapter_, write_router_.get());
```

**Remove from `initialize_mqtt_bridge_()`:** all `mqtt_bridge_*_init()` calls (the old C bridge inits). This function may become empty or be removed.

---

### Todo 4.2: Delete old C bridges and adapter dead code
**ID:** `delete-old-bridges`
**Depends on:** `bridge-orchestration` (must be verified working first)
**Output:** Deleted files + cleaned adapter

**Delete these files:**
- `mqtt_bridge.c`
- `mqtt_bridge.h`
- `mqtt_bridge_polling.c`
- `mqtt_bridge_polling.h`
- `mqtt_bridge_common.h`

**Remove from `esphome_mqtt_client_adapter.h/.cpp`:**
- `PendingErdUpdate` struct
- `pending_updates` map member
- `MAX_PENDING_UPDATES` constant
- `esphome_mqtt_client_adapter_drain_pending_updates()`
- `esphome_mqtt_client_adapter_notify_connected()`
- `mqtt_connected_at_ms` field

**Update Makefile / `CMakeLists.txt`:** remove `mqtt_bridge.c` and `mqtt_bridge_polling.c` from source lists.

---

### Todo 4.3: Update `StartupHsm` to populate `GlobalStateRegistry`
**ID:** `startup-hsm-registry`
**Depends on:** `global-state-registry`, `bridge-orchestration`
**Output:** Updated `geappliances_bridge_startup_hsm.cpp`

At each startup milestone, populate the registry (accessible via `g_bridge_services` until the static is refactored separately):
- After device ID assembled → `registry->set_device_id(device_id)`
- After appliance address found → `registry->set_appliance_address(addr)`
- After GEA2/GEA3 detected → `registry->set_gea_protocol_type(type)`

**Note:** The `g_bridge_services` static refactor is deferred. This todo only adds the `registry->set_*()` calls using the existing access pattern. The static-to-context-struct refactor is a separate follow-up change.

---

## Recommended Commit Structure

Split Phase 4 into three commits to keep each reviewable and bisectable:

1. **`wire new FSMs into GeappliancesBridge`**
   - `setup()` / `initialize_mqtt_client_()` construction changes
   - `loop()` switch-block removal + FSM `loop()` calls
   - connect/disconnect callback bodies updated
   - New FSM member declarations in `.h`

2. **`delete old C bridges and adapter cleanup`**
   - Delete `mqtt_bridge.c/.h`, `mqtt_bridge_polling.c/.h`, `mqtt_bridge_common.h`
   - Remove adapter `pending_updates` dead code
   - Remove `#include` references
   - Update Makefile

3. **`fix g_bridge_services static`** *(separate PR/branch, after Phase 4 verified)*
   - Move `IBridgeServices*` into HSM context struct
   - Update every state handler to use `ctx->bridge_services`
   - Pure refactor, no behavior change

---

## Phase Completion Checklist

- [ ] `esphome_mqtt_client_adapter_init()` called before `WriteRouter` is constructed
- [ ] `write_router_` and `mqtt_side_state_machine_` are `unique_ptr` members constructed in `initialize_mqtt_client_()`
- [ ] `GeappliancesBridge::loop()` calls `run_protocol_stack_()` first
- [ ] Inline `MqttConnectionState` switch block removed from `loop()`
- [ ] `ApplianceSideStateMachine::loop()` called in `loop()`
- [ ] `MqttSideStateMachine::loop()` called in `loop()`
- [ ] MQTT connect callback calls both `signal_mqtt_connected` AND `mqtt_fsm_->on_mqtt_connected()`
- [ ] MQTT disconnect callback calls both `notify_disconnected` AND `mqtt_fsm_->on_mqtt_disconnected()`
- [ ] Only mode-appropriate handlers constructed (no null-handler guards needed)
- [ ] `handle_erd_client_activity_()` retained (HA discovery + AUTO watchdog + device ID reads still use it)
- [ ] `StartupHsm` populates `GlobalStateRegistry` at each startup milestone
- [ ] `mqtt_bridge.c`, `mqtt_bridge.h`, `mqtt_bridge_polling.c`, `mqtt_bridge_polling.h`, `mqtt_bridge_common.h` deleted
- [ ] Adapter `pending_updates` map, `drain_pending_updates()`, `notify_connected()` removed
- [ ] No remaining `#include "mqtt_bridge.h"` or `#include "mqtt_bridge_polling.h"` in the codebase
- [ ] `g_bridge_services` static deferred to separate follow-up change
- [ ] `make test` passes (no regressions)
- [ ] Device connects to Home Assistant and publishes ERDs as before

---

## What This Unlocks

After Phase 4 is complete:
- The full new architecture is live end-to-end
- **Phase 5** verifies correctness with unit tests, integration tests, and backward compatibility checks
- The `g_bridge_services` static refactor can be done as a standalone low-risk cleanup
- `HaDiscoveryManager` migration to `ErdStateTable` events is a future todo (currently still driven by `handle_erd_client_activity_()`)
