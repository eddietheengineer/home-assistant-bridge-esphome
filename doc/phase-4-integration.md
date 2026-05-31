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
  run_protocol_stack_();              // 1. GEA2/GEA3 protocol — must be first
  appliance_fsm_->loop();            // 2. Appliance-side FSM
  mqtt_fsm_->loop();                 // 3. MQTT-side FSM
}
```

### The inline `MqttConnectionState` FSM is removed
`GeappliancesBridge::loop()` currently contains an inline `switch(mqtt_connection_state_)` block. After `MqttSideStateMachine` is wired in, this block is deleted. `MqttSideStateMachine::loop()` replaces it entirely.

### `g_bridge_services` file-scope static is fixed here
`geappliances_bridge_startup_hsm.cpp` holds a file-scope static `static IBridgeServices* g_bridge_services`. This prevents independent unit testing of the startup HSM because any test binary shares that pointer. Move `IBridgeServices*` into the HSM's context field (or a wrapper struct) so it is instance-owned, not globally shared.

### Delete old C bridges here
`mqtt_bridge.c`, `mqtt_bridge.h`, `mqtt_bridge_polling.c`, and `mqtt_bridge_polling.h` are deleted in this phase. Their appliance-side logic moved to Phase 2 handlers; their MQTT-publishing logic moved to `MqttSideStateMachine`. Double-check that no other file imports them before deleting.

---

## Todos

### Todo 4.1: Update `GeappliancesBridge` Orchestration
**ID:** `bridge-orchestration`
**Depends on:** `appliance-fsm`, `mqtt-fsm`
**Output:** Updated `geappliances_bridge.cpp` and `geappliances_bridge.h`

**Changes needed:**

**In `setup()`:**
- Instantiate `GlobalStateRegistry`
- Instantiate `ErdStateTable`
- Instantiate `WriteQueue`
- Instantiate `SubscriptionHandler`, `PollingHandler`, `WriteHandler`
- Instantiate `ApplianceSideStateMachine` (pass handlers + config)
- Instantiate `WriteRouter`
- Instantiate `MqttSideStateMachine`
- Wire MQTT connect/disconnect callbacks → `MqttSideStateMachine::on_mqtt_connected()` / `on_mqtt_disconnected()`

**In `loop()`:**
```cpp
void GeappliancesBridge::loop() {
  run_protocol_stack_();   // must remain first; GEA2 200ms busy-loop
  appliance_fsm_->loop();
  mqtt_fsm_->loop();
}
```
- Remove the inline `switch(mqtt_connection_state_)` block — it is replaced by `mqtt_fsm_->loop()`
- Remove direct calls to `mqtt_bridge_*` functions

**After wiring is verified:**
- Delete `mqtt_bridge.c`, `mqtt_bridge.h`
- Delete `mqtt_bridge_polling.c`, `mqtt_bridge_polling.h`
- Remove their includes from `geappliances_bridge.cpp`

---

### Todo 4.2: Update `StartupHsm` for `GlobalStateRegistry`
**ID:** `startup-hsm-refactor`
**Depends on:** `global-state-registry`
**Output:** Updated `geappliances_bridge_startup_hsm.cpp` (and `.h` if context struct changes)

**Changes needed:**

*Populate the registry during startup:*
- After device ID assembled → `registry->set_device_id(device_id)`
- After appliance address found → `registry->set_appliance_address(addr)`
- After GEA2/GEA3 detected → `registry->set_gea_protocol_type(type)`
- As startup stages complete → `registry->set_bridge_mode(NEW_MODE)`

*Fix the static global (Issue #8):*

Before (broken for unit testing):
```cpp
// geappliances_bridge_startup_hsm.cpp
static IBridgeServices* g_bridge_services = nullptr;

void set_bridge_services(IBridgeServices* services) {
  g_bridge_services = services;
}
```

After (instance-owned):
```cpp
// Move IBridgeServices* into the HSM context struct
struct StartupHsmContext {
  IBridgeServices* bridge_services;
  GlobalStateRegistry* registry;
  // ... other fields
};
```
Pass `StartupHsmContext` to the HSM rather than relying on the file-scope global. This allows each test to instantiate its own `StartupHsmContext` without shared state.

---

## Phase Completion Checklist

- [ ] `GeappliancesBridge::loop()` calls `run_protocol_stack_()` first
- [ ] Inline `MqttConnectionState` switch block removed from `GeappliancesBridge::loop()`
- [ ] `ApplianceSideStateMachine::loop()` called in `loop()`
- [ ] `MqttSideStateMachine::loop()` called in `loop()`
- [ ] MQTT connect/disconnect callbacks wired to `MqttSideStateMachine`
- [ ] `StartupHsm` populates `GlobalStateRegistry` at each startup milestone
- [ ] `g_bridge_services` file-scope static removed; `IBridgeServices*` is instance-owned in context
- [ ] `mqtt_bridge.c`, `mqtt_bridge.h`, `mqtt_bridge_polling.c`, `mqtt_bridge_polling.h` deleted
- [ ] No remaining `#include "mqtt_bridge.h"` or `#include "mqtt_bridge_polling.h"` in the codebase
- [ ] `make test` passes (no regressions)
- [ ] Device connects to Home Assistant and publishes ERDs as before

---

## What This Unlocks

After Phase 4 is complete:
- The full new architecture is live end-to-end
- **Phase 5** verifies correctness with unit tests, integration tests, and backward compatibility checks
- `HaDiscoveryManager` integration (deferred) can be planned: it currently stays driven by `handle_erd_client_activity_()` in `GeappliancesBridge` unchanged, with migration to `ErdStateTable` events as a future todo
