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

### Direct publish via `esphome_mqtt_client_adapter_publish()`, not the `pending_updates` map
`esphome_mqtt_client_adapter_publish()` is already in the public header and its implementation is literally `publish_now(topic, payload, retain)` — it does **not** go through `pending_updates`. No new function needed, no direct `esphome::mqtt::global_mqtt_client` access required.

`MqttSideStateMachine` reads flagged ERDs directly from `ErdStateTable`, calls `esphome_mqtt_client_adapter_publish()` with the same `MAX_FLUSH_PER_CALL` (≤5) limit per `loop()`, and clears each flag after the call. This eliminates double-buffering (`ErdStateTable` flags + adapter `pending_updates`). The adapter's `pending_updates` map becomes redundant and is removed in Phase 4 cleanup.

### `is_connected()` check — add to adapter's public C API
`publish_now()` internally checks `global_mqtt_client->is_connected()` and silently drops if disconnected. The FSM needs to check this **before** deciding whether to clear a flag. Add `esphome_mqtt_client_adapter_is_connected(const esphome_mqtt_client_adapter_t*)` to the adapter's public C API (one line internally). This keeps `MqttSideStateMachine` free of direct `global_mqtt_client` access and makes the connected-check mockable in unit tests.

### Topic/payload formatting — standalone free function
The hex/string formatting logic in `update_erd()` must move to a standalone free function in `erd_payload_formatter.h`:
```cpp
// Returns the MQTT payload string for an ERD value.
// Uses ErdRegistry::is_string_type() to choose hex vs. ASCII encoding.
std::string format_erd_payload(tiny_erd_t erd, const uint8_t* value, uint8_t size,
                               esphome::geappliances_bridge::ErdRegistry* registry);
```
This keeps `MqttSideStateMachine` thin and makes the formatter independently testable. Consequently, `MqttSideStateMachine` takes an `ErdRegistry*` constructor parameter and passes it to the formatter at call sites.

### Construction order constraint — adapter must be initialized before WriteRouter
`tiny_event_init()` is called inside `esphome_mqtt_client_adapter_init()`. Constructing `WriteRouter` and subscribing to `on_write_request_event` before `init()` is UB. The fix is a wiring constraint documented in Phase 4: `esphome_mqtt_client_adapter_init()` must be called before `WriteRouter` is constructed. No `late_subscribe()` needed.

### `get_flagged_erds()` vector — keep as-is
One `std::vector` allocation of ~100 × 2 bytes per loop at ~200 Hz is acceptable on ESP32 (the Phase 1 rationale applies). If profiling later shows heap fragmentation, add `void for_each_flagged_erd(void* ctx, bool(*cb)(void*, tiny_erd_t))` to `ErdStateTable`. Don't optimize prematurely.

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

### Todo 3.0: Add `esphome_mqtt_client_adapter_is_connected()` to adapter
**ID:** `adapter-is-connected`
**Output:** `esphome_mqtt_client_adapter.h` and `.cpp`

Add a single public C function so `MqttSideStateMachine` can check connection state without depending on `global_mqtt_client` directly:

```c
// Returns true if the underlying ESPHome MQTT client is connected.
bool esphome_mqtt_client_adapter_is_connected(
  const esphome_mqtt_client_adapter_t* self);
```

Implementation: `return esphome::mqtt::global_mqtt_client != nullptr && esphome::mqtt::global_mqtt_client->is_connected();`

---

### Todo 3.1: Create `erd_payload_formatter`
**ID:** `erd-payload-formatter`
**Depends on:** `erd-state-table`
**Output:** `components/geappliances_bridge/erd_payload_formatter.h` (and optionally `.cpp`)

Move the hex/string formatting logic out of `esphome_mqtt_client_adapter.cpp`'s `update_erd()` into a standalone free function so both the adapter (until Phase 4) and `MqttSideStateMachine` can use it without duplication:

```cpp
namespace esphome::geappliances_bridge {

// Returns the MQTT payload string for an ERD value.
// Uses ErdRegistry::is_string_type() to choose hex vs. ASCII encoding.
// registry may be nullptr, in which case hex encoding is always used.
std::string format_erd_payload(tiny_erd_t erd,
                               const uint8_t* value,
                               uint8_t size,
                               ErdRegistry* registry);

// Returns the MQTT value topic for an ERD.
// e.g. "geappliances/{device_id}/erd/0x1234/value"
std::string build_erd_topic(const std::string& device_id, tiny_erd_t erd);

} // namespace
```

**Note:** Do NOT modify the adapter's `update_erd()` in this phase. The adapter still uses its own inline logic until Phase 4 removes `pending_updates` and switches to call this helper.

---

### Todo 3.2: Create `WriteRouter`
**ID:** `write-router`
**Depends on:** `write-queue`
**Output:** `components/geappliances_bridge/write_router.h` and `.cpp`

Routes pre-parsed MQTT write events (from the adapter's `on_write_request` event) to `WriteQueue`.

**What it should do:**
- Subscribe to `mqtt_client_on_write_request()` event from the adapter at construction
- When the event fires, cast args to `mqtt_client_on_write_request_args_t*`, fill a `WriteCommand`, and push to `WriteQueue`
- Handle a full queue gracefully (log and discard the command)
- Does NOT call any MQTT subscribe function — that is `MqttSideStateMachine`'s job

**Construction order constraint:** `esphome_mqtt_client_adapter_init()` must be called before `WriteRouter` is constructed. Subscribing to `on_write_request_event` before `tiny_event_init()` has run on that event is UB. Document this in Phase 4 wiring.

**Public interface:**
```cpp
class WriteRouter {
public:
  // mqtt_client: the adapter that fires on_write_request events
  //   MUST be fully initialized (esphome_mqtt_client_adapter_init() already called)
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

### Todo 3.3: Create `MqttSideStateMachine`
**ID:** `mqtt-fsm`
**Depends on:** `adapter-is-connected`, `erd-payload-formatter`, `erd-state-table`, `global-state-registry`, `write-router`
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
    if (!esphome_mqtt_client_adapter_is_connected(adapter_) || flushed >= MAX_FLUSH_PER_CALL) break;
    uint8_t size = 0;
    const uint8_t* value = state_table_->get_erd_value(erd_id, size);
    if (value == nullptr) continue;
    std::string topic = build_erd_topic(*registry_->device_id(), erd_id);
    std::string payload = format_erd_payload(erd_id, value, size, erd_registry_);
    esphome_mqtt_client_adapter_publish(adapter_, topic, payload, /*retain=*/true);
    state_table_->clear_publish_flag(erd_id);
    flushed++;
  }
}
// Note: no on_mqtt_ack() — ESPHome publish is fire-and-forget at QoS=0
```

**Public interface:**
```cpp
class MqttSideStateMachine {
public:
  MqttSideStateMachine(ErdStateTable* state_table,
                       GlobalStateRegistry* registry,
                       ErdRegistry* erd_registry,          // for payload formatting
                       esphome_mqtt_client_adapter_t* adapter,
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

- [ ] `esphome_mqtt_client_adapter_is_connected()` added to adapter public API
- [ ] `erd_payload_formatter.h` compiles; `format_erd_payload()` and `build_erd_topic()` produce correct output
- [ ] `WriteRouter` compiles; subscribes to adapter's `on_write_request` event at construction
- [ ] `WriteRouter` does NOT call any MQTT subscribe function directly
- [ ] `MqttSideStateMachine` compiles with 5 constructor parameters
- [ ] `MqttSideStateMachine` has **no** `on_mqtt_ack()` method
- [ ] Publish flags cleared only after confirmed `esphome_mqtt_client_adapter_publish()` call while connected
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
