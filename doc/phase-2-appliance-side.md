# Phase 2: Appliance-Side Decoupling

## Purpose

Extract the appliance-side logic from the existing C bridges into focused, testable C++ handlers, then wire them together with a coordinating `ApplianceSideStateMachine`. After this phase, the appliance side no longer calls the MQTT client directly — it only writes to the `ErdStateTable`.

---

## Prerequisites (Phase 1 complete)

- `GlobalStateRegistry` — used to know when `appliance_address` has been discovered
- `ErdStateTable` — handlers write ERD values here instead of calling MQTT directly
- `WriteQueue` — `WriteHandler` consumes from this queue
- Familiarity with `mqtt_bridge.c` (subscription source) and `mqtt_bridge_polling.c` (polling source) — these are the files being replaced

---

## Key Design Decisions for This Phase

### Handlers replace the existing C bridges (appliance side only)
`mqtt_bridge.c` and `mqtt_bridge_polling.c` each handle **both** appliance-side ERD reading **and** MQTT publishing in one unit. This phase extracts only the appliance-side reading half:

| Old code | Replaced by |
|----------|-------------|
| `mqtt_bridge.c` (subscription + publish) | `SubscriptionHandler` (subscription only) |
| `mqtt_bridge_polling.c` (polling + publish) | `PollingHandler` (polling only) |

The MQTT publishing half moves to `MqttSideStateMachine` in Phase 3. **Neither `SubscriptionHandler` nor `PollingHandler` should call the MQTT client directly.** The old files are deleted at the end of Phase 3.

### `only_publish_on_change` is an appliance-side decision
Whether to publish every poll or only on change is determined by the polling configuration — it is not the MQTT side's concern. Handlers enforce this by choosing which `ErdStateTable` write method to call:
- `only_publish_on_change = true` → call `update_erd_value()` (flag set only if value changed)
- `only_publish_on_change = false` → call `update_erd_value()` then `set_publish_flag()` (unconditional)

### `ApplianceSideStateMachine::loop()` must not block
`GeappliancesBridge::run_protocol_stack_()` runs a 200 ms wall-clock busy loop for GEA2 hardware timing. This loop belongs at the `GeappliancesBridge` level and runs **before** any FSM `loop()` calls. `ApplianceSideStateMachine::loop()` must be a fast, non-blocking call that only advances FSM state and delegates to handlers.

---

## Todos

### Todo 2.1: Create `SubscriptionHandler`
**ID:** `subscription-handler`
**Depends on:** `erd-state-table`, `global-state-registry`
**Output:** `components/geappliances_bridge/subscription_handler.h` and `.cpp`

Extract the appliance-side subscription logic from `mqtt_bridge.c`. Writes ERD updates to `ErdStateTable`. Does **not** call the MQTT client.

**What it should do:**
- Use GEA3 ERD client to subscribe to a configured set of ERDs
- When subscription updates arrive, call `erd_state_table->update_erd_value()`
- Handle subscription lifecycle (start, stop, error recovery)

**Public interface:**
```cpp
class SubscriptionHandler {
public:
  SubscriptionHandler(i_tiny_gea3_erd_client_t* erd_client,
                      ErdStateTable* state_table);

  void subscribe_to_erds(const std::vector<tiny_erd_t>& erd_list);
  void unsubscribe_all();
  bool is_subscribed_to(tiny_erd_t erd_id) const;

  // Called by the GEA3 client callback when an update arrives
  void handle_erd_update(tiny_erd_t erd_id, const uint8_t* value, uint8_t size);
};
```

**Reference:** `mqtt_bridge.c` — copy subscription setup/teardown logic; remove the MQTT publish call.

---

### Todo 2.2: Create `PollingHandler`
**ID:** `polling-handler`
**Depends on:** `erd-state-table`, `global-state-registry`
**Output:** `components/geappliances_bridge/polling_handler.h` and `.cpp`

Extract the appliance-side polling logic from `mqtt_bridge_polling.c`. Writes ERD updates to `ErdStateTable`. Does **not** call the MQTT client.

**What it should do:**
- Use GEA3 ERD client to poll a configured set of ERDs on an interval
- When poll responses arrive:
  - Always call `erd_state_table->update_erd_value()`
  - If `only_publish_on_change = false`, also call `erd_state_table->set_publish_flag()`
- Handle polling errors and retry logic

**Public interface:**
```cpp
class PollingHandler {
public:
  PollingHandler(i_tiny_gea3_erd_client_t* erd_client,
                 ErdStateTable* state_table,
                 uint32_t polling_interval_ms,
                 bool only_publish_on_change);

  void add_erd_to_poll(tiny_erd_t erd_id);
  void remove_erd_from_poll(tiny_erd_t erd_id);
  void set_polling_interval(uint32_t ms);
  void poll_now(); // Trigger immediate poll (e.g., on reconnect)

  // Called by the GEA3 client callback when a poll response arrives
  void handle_poll_response(tiny_erd_t erd_id, const uint8_t* value, uint8_t size);
};
```

**Reference:** `mqtt_bridge_polling.c` — copy poll timer/ERD list logic; remove the MQTT publish call.

---

### Todo 2.3: Create `WriteHandler`
**ID:** `write-handler`
**Depends on:** `write-queue`, `global-state-registry`
**Output:** `components/geappliances_bridge/write_handler.h` and `.cpp`

Consumes write commands from `WriteQueue` and sends them to the appliance via the GEA3 ERD client.

**What it should do:**
- In each `process_writes()` call, check the write queue for pending commands
- For each command, send the write to the appliance using the GEA3 ERD client
- Clear the command from the queue after a successful send (or after N retries)
- Log/handle write failures

**Public interface:**
```cpp
class WriteHandler {
public:
  WriteHandler(i_tiny_gea3_erd_client_t* erd_client,
               WriteQueue* write_queue);

  void process_writes(); // Called from FSM loop each cycle

  // Called by GEA3 client callback when write response arrives
  void on_write_response(tiny_erd_t erd_id, bool success);
};
```

---

### Todo 2.4: Create `ApplianceSideStateMachine`
**ID:** `appliance-fsm`
**Depends on:** `erd-state-table`, `global-state-registry`, `subscription-handler`, `polling-handler`, `write-handler`
**Output:** `components/geappliances_bridge/appliance_side_state_machine.h` and `.cpp`

Coordinates the three handlers based on configuration. Transitions to Running when the appliance address is discovered.

**States:**
- **Idle** — waiting for appliance discovery (`appliance_address == 0`)
- **Running** — actively delegating to handlers
- **Error** — handler failure; will retry after delay

**Transitions:**
- `Idle → Running`: when `GlobalStateRegistry::on_appliance_address_changed` fires
- `Running → Error`: when handlers report repeated failures
- `Error → Running`: after retry delay expires

**Configuration:**
```cpp
struct ApplianceSideConfig {
  bool enable_subscriptions;
  bool enable_polling;
  uint32_t polling_interval_ms;
  bool only_publish_on_change;          // passed to PollingHandler
  std::vector<tiny_erd_t> subscription_erds;
  std::vector<tiny_erd_t> polling_erds;
};
```

**Public interface:**
```cpp
class ApplianceSideStateMachine {
public:
  ApplianceSideStateMachine(ErdStateTable* state_table,
                            GlobalStateRegistry* registry,
                            WriteQueue* write_queue,
                            i_tiny_gea3_erd_client_t* erd_client);

  void set_config(const ApplianceSideConfig& config);
  void loop();  // Fast, non-blocking; called from GeappliancesBridge::loop()
  State get_current_state() const;
};
```

**Important:** `loop()` must not block. The 200 ms GEA2 busy-loop is handled by `GeappliancesBridge::run_protocol_stack_()` before this FSM's `loop()` is called.

---

## Phase Completion Checklist

- [ ] `SubscriptionHandler` compiles and does not import any MQTT headers
- [ ] `PollingHandler` compiles and does not import any MQTT headers
- [ ] `WriteHandler` compiles
- [ ] `ApplianceSideStateMachine` compiles
- [ ] Unit tests pass for `SubscriptionHandler` (Phase 5 Todo 5.3)
- [ ] Unit tests pass for `PollingHandler` (Phase 5 Todo 5.4)
- [ ] Unit tests pass for `WriteHandler` (Phase 5 Todo 5.5)
- [ ] Unit tests pass for `ApplianceSideStateMachine` (Phase 5 Todo 5.6)
- [ ] `mqtt_bridge.c` and `mqtt_bridge_polling.c` are **not yet deleted** (they are still wired in `GeappliancesBridge` and will be removed in Phase 4)
- [ ] `make test` still passes (no regressions)

---

## What This Unlocks

After Phase 2 is complete:
- The appliance side has a clean, testable interface through `ApplianceSideStateMachine`
- **Phase 3** can implement `MqttSideStateMachine`, which reads from the `ErdStateTable` that the handlers now populate
- **Phase 4** can replace the direct calls to `mqtt_bridge.c`/`mqtt_bridge_polling.c` in `GeappliancesBridge` with the new FSMs
