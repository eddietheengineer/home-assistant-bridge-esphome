# Phase 3: MQTT-Side Decoupling

## Purpose

Extract the MQTT-side logic into focused, testable C++ modules. After this phase, MQTT publishing is handled exclusively by `MqttSideStateMachine`, which reads from `ErdStateTable` — it has no direct knowledge of the appliance protocol. Write commands from MQTT are routed through `WriteRouter` to `WriteQueue` for the appliance side to consume.

**Important:** During Phase 3, `MqttSideStateMachine` is built and unit-tested in isolation. It is **not** wired into `GeappliancesBridge`. The old inline FSM and old C bridges continue running unchanged. The cutover happens atomically in Phase 4.

---

## Prerequisites (Phases 1–2 complete)

- `ErdStateTable` — `MqttSideStateMachine` reads flagged ERDs from here and clears flags after publishing
- `GlobalStateRegistry` — provides `device_id` for topic construction
- `WriteQueue` — `WriteRouter` pushes incoming MQTT write commands here
- `ApplianceSideStateMachine` — already built (Phase 2), not yet wired into the bridge
- Existing inline `MqttConnectionState` FSM in `GeappliancesBridge::loop()` — Phase 3 **extracts** this existing logic; it is not written from scratch

---

## Key Design Decisions for This Phase

### WriteRouter does NOT do its own MQTT subscribe
`esphome_mqtt_client_adapter_subscribe_write_topic()` already handles all of it: topic construction from device_id, idempotency (`wildcard_subscribed` flag), the ESPHome lambda registration, and topic/payload parsing. `MqttSideStateMachine` calls this function in its SUBSCRIBING state (identical to the current code). `WriteRouter` has no involvement in triggering the subscribe.

### WriteRouter receives pre-parsed events, not raw MQTT messages
The adapter's lambda parses the topic → ERD ID and payload → byte array, then fires `on_write_request_event` with `mqtt_client_on_write_request_args_t`. `WriteRouter` subscribes to `mqtt_client_on_write_request()` (the `tiny_event_t`) and in its callback pushes to `WriteQueue`. No raw `esphome::mqtt::global_mqtt_client` access needed.

### Direct `publish_now()`, not the adapter's `pending_updates` map
`esphome_mqtt_client_adapter_publish()` is synchronous `publish_now()` — there is no separate FreeRTOS task owned by this code. The "async" aspect is the IDF MQTT outbox inside ESPHome, which is invisible to us. The adapter's `pending_updates` map is a non-blocking rate-limiter (MAX_FLUSH_PER_CALL = 5 per loop), not an async queue.

`MqttSideStateMachine` reads flagged ERDs directly from `ErdStateTable` and calls `publish_now()` with the same MAX_FLUSH_PER_CALL limit enforced inside `loop()`. This eliminates double-buffering (`ErdStateTable` flags + adapter `pending_updates`). The adapter's `pending_updates` map becomes redundant and is removed in Phase 4 cleanup.

### Flag clearing: only after confirmed publish call
Clear `publish_flag` only after `publish_now()` was actually called (i.e., MQTT was still connected at that moment). If MQTT disconnects mid-drain, skip clearing for unpublished ERDs — their flags stay set and are flushed on the next reconnect.

```cpp
size_t flushed = 0;
for (auto erd_id : state_table_->get_flagged_erds()) {
  if (!is_mqtt_connected() || flushed >= MAX_FLUSH_PER_CALL) break;
  publish_now(build_topic(erd_id), get_payload(erd_id), /*retain=*/true);
  state_table_->clear_publish_flag(erd_id); // only after confirmed publish call
  flushed++;
}
// Note: no on_mqtt_ack() — ESPHome publish is fire-and-forget at QoS=0
```

### FLUSHING state is kept
FLUSHING and RUNNING both drain flagged ERDs, but keeping the distinction is useful: FLUSHING represents "catching up the backlog after reconnect" while RUNNING is steady-state. Transition FLUSHING → RUNNING when `get_flagged_erds().empty()`.

### `signal_mqtt_connected` stays in `GeappliancesBridge`, not in `MqttSideStateMachine`
`GeappliancesBridge` already calls `tiny_hsm_send_signal(&startup_hsm_, signal_mqtt_connected, nullptr)` on the connect edge. In Phase 4, it will also call `mqtt_fsm_->on_mqtt_connected()` at the same point. `MqttSideStateMachine` has no dependency on the startup HSM.

### No dual-publishing risk during Phase 3
`MqttSideStateMachine` is unit-tested in isolation in Phase 3. The old inline FSM and old C bridges run unchanged in `GeappliancesBridge`. The cutover in Phase 4 is a single atomic change: wire the new FSM, remove the inline FSM switch block, and delete the old C bridges in one step.

---

## Todos

### Todo 3.1: Create `WriteRouter`
**ID:** `write-router`
**Depends on:** `write-queue`
**Output:** `components/geappliances_bridge/write_router.h` and `.cpp`

Routes pre-parsed MQTT write events (from the adapter's `on_write_request` event) to `WriteQueue`.

**What it should do:**
- Subscribe to `mqtt_client_on_write_request()` event from the adapter at construction
- When the event fires, cast args to `mqtt_client_on_write_request_args_t*`, fill a `WriteCommand`, and push to `WriteQueue`
- Handle a full queue gracefully (log and discard the command)
- Does NOT call any MQTT subscribe function — that is `MqttSideStateMachine`'s job

**Public interface:**
```cpp
class WriteRouter {
public:
  // mqtt_client: the adapter that fires on_write_request events
  // write_queue: destination for parsed write commands
  WriteRouter(i_mqtt_client_t* mqtt_client, WriteQueue* write_queue);

private:
  tiny_event_subscription_t write_request_subscription_;
  WriteQueue* write_queue_;

  static void on_write_request_handler(void* context, const void* args);
};
```

**Note:** The existing parsing logic (topic → ERD ID, payload → bytes) lives in the adapter lambda and is already done before the event fires. `WriteRouter` receives fully-parsed `erd`, `size`, and `value` — no additional parsing needed.

---

### Todo 3.2: Create `MqttSideStateMachine`
**ID:** `mqtt-fsm`
**Depends on:** `erd-state-table`, `global-state-registry`, `write-router`
**Output:** `components/geappliances_bridge/mqtt_side_state_machine.h` and `.cpp`

Extract and formalize the inline `MqttConnectionState` FSM from `GeappliancesBridge::loop()` into a standalone class. This is an extraction of existing logic, not new logic. Additionally, replace the adapter's `pending_updates` drain with a direct `ErdStateTable`-based publish loop.

**States** (map directly from the existing inline FSM):

| New state | Existing `MqttConnectionState` |
|-----------|-------------------------------|
| `Disconnected` | `DISCONNECTED` |
| `Subscribing` | `SUBSCRIBING` |
| `Flushing` | `FLUSHING` |
| `Running` | `RUNNING` |

**State behavior:**
- **Disconnected** — no MQTT connection; `publish_flag`s accumulate in `ErdStateTable` (untouched)
- **Subscribing** — connected; calls `esphome_mqtt_client_adapter_subscribe_write_topic()`; transitions to Flushing immediately after (same as existing code)
- **Flushing** — each `loop()`: publish up to `MAX_FLUSH_PER_CALL` flagged ERDs; transition to Running when `get_flagged_erds().empty()`
- **Running** — each `loop()`: publish up to `MAX_FLUSH_PER_CALL` newly-flagged ERDs

**Publish loop (used in both Flushing and Running):**
```cpp
static constexpr size_t MAX_FLUSH_PER_CALL = 5;

void MqttSideStateMachine::drain_flagged_erds_() {
  size_t flushed = 0;
  for (auto erd_id : state_table_->get_flagged_erds()) {
    if (!is_connected_() || flushed >= MAX_FLUSH_PER_CALL) break;
    uint8_t size = 0;
    const uint8_t* value = state_table_->get_erd_value(erd_id, size);
    if (value == nullptr) continue;
    publish_now_(build_topic_(erd_id), format_payload_(erd_id, value, size), /*retain=*/true);
    state_table_->clear_publish_flag(erd_id);
    flushed++;
  }
}
// Note: no on_mqtt_ack() — ESPHome publish is fire-and-forget at QoS=0
```

**Topic/payload formatting:** Replicate the topic construction and hex/string payload formatting from `esphome_mqtt_client_adapter.cpp` (`update_erd()` function). Move this logic into `MqttSideStateMachine` or a shared helper. String-typed ERD detection still uses `ErdRegistry::is_string_type()`.

**Public interface:**
```cpp
class MqttSideStateMachine {
public:
  MqttSideStateMachine(ErdStateTable* state_table,
                       GlobalStateRegistry* registry,
                       esphome_mqtt_client_adapter_t* adapter, // for subscribe_write_topic + publish_now
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

- [ ] `WriteRouter` compiles; subscribes to adapter's `on_write_request` event at construction
- [ ] `WriteRouter` does NOT call any MQTT subscribe function directly
- [ ] `MqttSideStateMachine` compiles
- [ ] `MqttSideStateMachine` has **no** `on_mqtt_ack()` method
- [ ] Publish flags cleared only when `is_connected()` is true at time of publish
- [ ] `MAX_FLUSH_PER_CALL` (≤5) enforced per `loop()` call
- [ ] `signal_mqtt_connected` is **not** sent from inside `MqttSideStateMachine`
- [ ] Unit tests pass for `WriteRouter` (Phase 5 Todo 5.8)
- [ ] Unit tests pass for `MqttSideStateMachine` (Phase 5 Todo 5.7)
- [ ] Neither `WriteRouter` nor `MqttSideStateMachine` is wired into `GeappliancesBridge` yet — that is Phase 4
- [ ] `make test` still passes (no regressions in existing code)

---

## What This Unlocks

After Phase 3 is complete:
- Both FSMs exist as standalone, testable classes
- **Phase 4** atomically wires them in: replaces the inline `MqttConnectionState` FSM, removes the adapter's `pending_updates` map, wires `WriteRouter` to replace `handle_erd_write_()`, sends `signal_mqtt_connected` alongside `mqtt_fsm_->on_mqtt_connected()`, and deletes `mqtt_bridge.c`/`mqtt_bridge_polling.c`
- Integration testing (Phase 5) can begin once Phase 4 is done
