# Phase 3: MQTT-Side Decoupling

## Purpose

Extract the MQTT-side logic into focused, testable C++ modules. After this phase, MQTT publishing is handled exclusively by `MqttSideStateMachine`, which reads from `ErdStateTable` — it has no direct knowledge of the appliance protocol. Write commands from MQTT are routed through `WriteRouter` to `WriteQueue` for the appliance side to consume.

At the end of this phase, `mqtt_bridge.c` and `mqtt_bridge_polling.c` are **deleted** — their MQTT-publishing logic has been folded into `MqttSideStateMachine`.

---

## Prerequisites (Phases 1–2 complete)

- `ErdStateTable` — `MqttSideStateMachine` reads flagged ERDs from here and clears flags after publishing
- `GlobalStateRegistry` — provides `device_id` for topic construction
- `WriteQueue` — `WriteRouter` pushes incoming MQTT write commands here
- `ApplianceSideStateMachine` — already wired and running (appliance side is decoupled)
- Existing inline `MqttConnectionState` FSM in `GeappliancesBridge::loop()` — Phase 3 **extracts** this existing logic into `MqttSideStateMachine`; it is not written from scratch

---

## Key Design Decisions for This Phase

### The inline FSM already exists — extract, don't rewrite
`GeappliancesBridge::loop()` already contains a `MqttConnectionState` enum with states `DISCONNECTED → SUBSCRIBING → FLUSHING → RUNNING`. `MqttSideStateMachine` is the standalone-class form of this logic. Copy the state machine logic from `GeappliancesBridge::loop()` into the new class and remove it from the bridge.

### Synchronous flag clearing — no broker ACK callback
ESPHome's `MQTTClientComponent::publish()` is fire-and-forget at QoS=0. There is no per-message broker ACK callback. Publish flags **must** be cleared immediately after `publish()` returns — not in a callback, not after a timer. `MqttSideStateMachine` does not have and must not expose an `on_mqtt_ack()` method.

### `mqtt_bridge.c` and `mqtt_bridge_polling.c` are deleted here
After `MqttSideStateMachine` is wired in Phase 4, these files serve no purpose. Mark their deletion as part of the Phase 4 orchestration todo. Plan for it now by ensuring `MqttSideStateMachine` covers all the MQTT-publishing paths they contained.

---

## Todos

### Todo 3.1: Create `WriteRouter`
**ID:** `write-router`
**Depends on:** `write-queue`, `global-state-registry`
**Output:** `components/geappliances_bridge/write_router.h` and `.cpp`

Subscribes to the MQTT write topic and routes parsed write commands to the `WriteQueue`.

**What it should do:**
- Subscribe to the MQTT wildcard write topic: `geappliances/{device_id}/erd/+/write`
- Parse the ERD ID from the topic and the value from the payload
- Push a `WriteCommand` to `WriteQueue` for the appliance side to process
- Handle malformed topics/payloads gracefully (log and discard)

**Public interface:**
```cpp
class WriteRouter {
public:
  WriteRouter(i_mqtt_client_t* mqtt_client,
              WriteQueue* write_queue,
              const std::string& device_id);

  // Called during MQTT Subscribing state to register the wildcard write topic
  void subscribe_to_write_topic();

  // MQTT client calls this when a message arrives on the write topic
  void on_write_received(const std::string& topic, const std::string& payload);
};
```

---

### Todo 3.2: Create `MqttSideStateMachine`
**ID:** `mqtt-fsm`
**Depends on:** `erd-state-table`, `global-state-registry`, `write-router`
**Output:** `components/geappliances_bridge/mqtt_side_state_machine.h` and `.cpp`

Extract and formalize the inline `MqttConnectionState` FSM from `GeappliancesBridge::loop()` into a standalone class. This is an extraction of existing logic, not new logic.

**States** (map directly from the existing inline FSM):

| New state | Existing `MqttConnectionState` |
|-----------|-------------------------------|
| `Disconnected` | `DISCONNECTED` |
| `Subscribing` | `SUBSCRIBING` |
| `Flushing` | `FLUSHING` |
| `Running` | `RUNNING` |

**State behavior:**
- **Disconnected** — no MQTT connection; `publish_flag`s accumulate in `ErdStateTable` (untouched)
- **Subscribing** — connected; calls `WriteRouter::subscribe_to_write_topic()`; transitions to Flushing when done
- **Flushing** — drains all currently-flagged ERDs; transitions to Running when none remain
- **Running** — each `loop()` call: check for flagged ERDs, publish each, clear flag synchronously

**Flag clearing (critical):**
```cpp
for (auto erd_id : state_table_->get_flagged_erds()) {
  auto value = state_table_->get_erd_value(erd_id, size);
  mqtt_client_->publish(build_topic(erd_id), value, size);
  state_table_->clear_publish_flag(erd_id); // immediately after publish()
}
// Note: no on_mqtt_ack() — ESPHome publish is fire-and-forget at QoS=0
```

**Public interface:**
```cpp
class MqttSideStateMachine {
public:
  MqttSideStateMachine(ErdStateTable* state_table,
                       GlobalStateRegistry* registry,
                       i_mqtt_client_t* mqtt_client,
                       WriteRouter* write_router);

  void loop();  // Fast, non-blocking; called from GeappliancesBridge::loop()
  State get_current_state() const;

  // Called by GeappliancesBridge when MQTT connect/disconnect events fire
  void on_mqtt_connected();
  void on_mqtt_disconnected();

  // Note: no on_mqtt_ack() — ESPHome publish is fire-and-forget at QoS=0
};
```

---

## Phase Completion Checklist

- [ ] `WriteRouter` compiles
- [ ] `MqttSideStateMachine` compiles
- [ ] `MqttSideStateMachine` has **no** `on_mqtt_ack()` method
- [ ] Flags are cleared inside the publish loop, immediately after `publish()` returns
- [ ] Unit tests pass for `WriteRouter` (Phase 5 Todo 5.8)
- [ ] Unit tests pass for `MqttSideStateMachine` (Phase 5 Todo 5.7)
- [ ] `mqtt_bridge.c` and `mqtt_bridge_polling.c` are ready to be deleted (their logic is now in `SubscriptionHandler`, `PollingHandler`, and `MqttSideStateMachine`)
- [ ] `make test` still passes (no regressions)

---

## What This Unlocks

After Phase 3 is complete:
- Both FSMs exist as standalone, testable classes
- **Phase 4** wires them into `GeappliancesBridge`, replaces the inline `MqttConnectionState` FSM, deletes `mqtt_bridge.c` and `mqtt_bridge_polling.c`, and updates `StartupHsm` to populate `GlobalStateRegistry`
- Integration testing (Phase 5) can begin once Phase 4 is done
