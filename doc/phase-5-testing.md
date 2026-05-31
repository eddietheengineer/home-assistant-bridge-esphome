# Phase 5: Testing & Verification

## Purpose

Verify that all new modules are correct in isolation (unit tests) and that the full bridge works end-to-end (integration tests). Also confirm that no existing functionality has regressed.

Unit tests for each module can be written **during** the phase that implements that module — they do not have to wait until Phase 5. This document collects all test specs in one place.

---

## Prerequisites (Phases 1–4 complete)

- All new modules implemented and wired
- `make test` runs the CppUTest unit test suite
- `make pytest` runs the Python generator tests
- Both must pass before Phase 5 is considered complete

---

## Running Tests

```bash
# C++ unit tests (CppUTest)
make test

# Python generator tests
make pytest

# Run both
make test && make pytest
```

---

## Unit Tests

### Todo 5.1: Unit Tests — `GlobalStateRegistry`
**ID:** `unit-tests-global-registry`
**Depends on:** `global-state-registry`
**Output:** `test/test_global_state_registry.cpp`

**Test cases:**
- Read returns correct value after `set_*()` called
- `on_appliance_address_changed` fires when `set_appliance_address()` called
- `on_gea_protocol_type_changed` fires when `set_gea_protocol_type()` called
- `on_bridge_mode_changed` fires when `set_bridge_mode()` called
- Multiple subscribers for the same field are all called
- Unsubscribing prevents callback from firing
- `set_device_id()` does not fire an event (write-once, no subscription)

---

### Todo 5.2: Unit Tests — `ErdStateTable`
**ID:** `unit-tests-erd-state`
**Depends on:** `erd-state-table`
**Output:** `test/test_erd_state_table.cpp`

**Test cases:**
- `update_erd_value()` with a **new** value → `publish_flag = true`, `on_erd_changed` fires
- `update_erd_value()` with the **same** value → `publish_flag` stays false, `on_erd_changed` does not fire
- `set_publish_flag()` sets flag unconditionally even when value is unchanged
- `clear_publish_flag()` clears the flag
- `get_flagged_erds()` returns only entries where `publish_flag == true`
- `get_erd_value()` returns correct value and size after update
- `has_flag()` returns correct boolean
- Value larger than `MAX_ERD_VALUE_SIZE` is rejected/clamped (no buffer overwrite)
- Multiple ERDs tracked independently

---

### Todo 5.3: Unit Tests — `SubscriptionHandler`
**ID:** `unit-tests-subscription-handler`
**Depends on:** `subscription-handler`
**Output:** `test/test_subscription_handler.cpp`

**Test cases:**
- `subscribe_to_erds()` triggers subscription calls on the mock GEA3 client
- Subscription update → `ErdStateTable::update_erd_value()` called with correct args
- `is_subscribed_to()` returns true after subscribe, false after `unsubscribe_all()`
- `unsubscribe_all()` removes all subscriptions
- Error on subscription attempt is handled without crash

---

### Todo 5.4: Unit Tests — `PollingHandler`
**ID:** `unit-tests-polling-handler`
**Depends on:** `polling-handler`
**Output:** `test/test_polling_handler.cpp`

**Test cases:**
- Poll interval triggers poll on the mock GEA3 client
- Poll response with **changed** value: `update_erd_value()` called → `publish_flag` set
- Poll response with **same** value, `only_publish_on_change = true`: flag stays clear
- Poll response with **same** value, `only_publish_on_change = false`: `set_publish_flag()` called → flag set
- `add_erd_to_poll()` / `remove_erd_from_poll()` updates the poll list correctly
- `poll_now()` triggers an immediate poll regardless of interval
- Poll failure is retried; after N failures the ERD is skipped for that cycle

---

### Todo 5.5: Unit Tests — `WriteHandler`
**ID:** `unit-tests-write-handler`
**Depends on:** `write-handler`
**Output:** `test/test_write_handler.cpp`

**Test cases:**
- `process_writes()` dequeues a command and sends it to the mock GEA3 client
- `on_write_response(success=true)` removes the command from the queue
- `on_write_response(success=false)` retries; after N failures the command is dropped
- Multiple pending writes are processed in FIFO order
- Empty queue is a no-op

---

### Todo 5.6: Unit Tests — `ApplianceSideStateMachine`
**ID:** `unit-tests-appliance-fsm`
**Depends on:** `appliance-fsm`
**Output:** `test/test_appliance_side_fsm.cpp`

**Test cases:**
- Starts in `Idle` state
- `Idle → Running` when `GlobalStateRegistry::on_appliance_address_changed` fires
- In `Running`, subscription handler is active when `enable_subscriptions = true`
- In `Running`, polling handler is active when `enable_polling = true`
- In `Running`, write handler processes queue commands
- `Running → Error` when handlers report repeated failures
- `Error → Running` after retry delay

---

### Todo 5.7: Unit Tests — `MqttSideStateMachine`
**ID:** `unit-tests-mqtt-fsm`
**Depends on:** `mqtt-fsm`
**Output:** `test/test_mqtt_side_fsm.cpp`

**Test cases:**
- Starts in `Disconnected` state
- `on_mqtt_connected()` transitions to `Subscribing`
- In `Subscribing`, `WriteRouter::subscribe_to_write_topic()` is called; transitions to `Flushing`
- In `Flushing`, all flagged ERDs are published; transitions to `Running` when none remain
- In `Running`, `loop()` publishes newly-flagged ERDs each cycle
- Publish flag cleared **immediately after `publish()` call** (verify no second publish attempt)
- `on_mqtt_disconnected()` transitions to `Disconnected`; flags accumulate in state table
- On reconnect (second `on_mqtt_connected()`), accumulated flags are all published

---

### Todo 5.8: Unit Tests — `WriteRouter`
**ID:** `unit-tests-write-router`
**Depends on:** `write-router`
**Output:** `test/test_write_router.cpp`

**Test cases:**
- `subscribe_to_write_topic()` registers the correct wildcard topic on the mock MQTT client
- Valid write message parsed: ERD ID extracted from topic, value from payload, `WriteQueue::push()` called
- Invalid topic format handled gracefully (no crash, no push)
- Invalid payload format handled gracefully (no crash, no push)
- Multiple writes queued in order

---

## Integration Tests

### Todo 5.9: Integration Test — Basic Flow
**ID:** `integration-test-basic`
**Depends on:** `appliance-fsm`, `mqtt-fsm`
**Output:** `test/test_integration_basic.cpp`

**Scenario:**
1. Start with appliance address set in `GlobalStateRegistry`
2. Verify `ApplianceSideStateMachine` transitions to `Running`
3. Call `on_mqtt_connected()` on `MqttSideStateMachine`; verify it reaches `Running`
4. Simulate ERD subscription update via `SubscriptionHandler::handle_erd_update()`
5. Verify `ErdStateTable` updated with correct value and `publish_flag = true`
6. Verify `MqttSideStateMachine::loop()` publishes the ERD to mock MQTT client
7. Verify `publish_flag` cleared synchronously after `publish()` returns

---

### Todo 5.10: Integration Test — Disconnection Recovery
**ID:** `integration-test-disconnect`
**Depends on:** `appliance-fsm`, `mqtt-fsm`
**Output:** `test/test_integration_disconnect.cpp`

**Scenarios:**
- **MQTT disconnect:** call `on_mqtt_disconnected()`; simulate ERD updates; verify flags accumulate but `publish()` is not called
- **MQTT reconnect:** call `on_mqtt_connected()`; verify all accumulated flags are published in Flushing state
- **Appliance disconnect:** simulate appliance FSM entering `Error`; verify no further writes/reads attempted
- **Appliance reconnect:** simulate appliance FSM re-entering `Running`; verify subscription/polling resumes

---

### Todo 5.11: Integration Test — Write Commands
**ID:** `integration-test-writes`
**Depends on:** `write-handler`, `write-router`
**Output:** `test/test_integration_writes.cpp`

**Scenario:**
1. MQTT write message arrives on write topic
2. Verify `WriteRouter` parses and pushes `WriteCommand` to `WriteQueue`
3. Verify `WriteHandler::process_writes()` dequeues and sends to mock GEA3 client
4. Simulate successful write response
5. Verify `WriteQueue` is empty after success

---

### Todo 5.12: Backward Compatibility Verification
**ID:** `backward-compat-verification`
**Depends on:** All integration tests above
**Output:** Test report (no new code)

Run the full existing test suites and confirm no regressions:

```bash
make test    # CppUTest unit tests
make pytest  # Python generator tests
```

**Verify manually (or with hardware):**
- Polling mode still works
- Subscription mode still works
- Mixed (polling + subscription) mode works
- Device ID generation unchanged
- ERD registry validation unchanged
- Home Assistant discovery publishes correctly
- No regressions in memory usage or timing on device

---

### Todo 5.13: Update Architecture Documentation
**ID:** `documentation-update`
**Depends on:** All previous todos
**Output:** Updated files in `doc/`

**What to document:**
- Data flow diagrams: appliance side → `ErdStateTable` → MQTT side
- State machine diagrams for both FSMs
- Module responsibilities and public interfaces
- Event subscription map (who fires, who listens)
- Configuration options (`ApplianceSideConfig`)
- Migration guide noting deleted files (`mqtt_bridge.c`, `mqtt_bridge_polling.c`)
- Deferred items: `HaDiscoveryManager` migration, multi-appliance support, Flash persistence

---

## Summary

| Todo | Module | Type |
|------|--------|------|
| 5.1 | `GlobalStateRegistry` | Unit |
| 5.2 | `ErdStateTable` | Unit |
| 5.3 | `SubscriptionHandler` | Unit |
| 5.4 | `PollingHandler` | Unit |
| 5.5 | `WriteHandler` | Unit |
| 5.6 | `ApplianceSideStateMachine` | Unit |
| 5.7 | `MqttSideStateMachine` | Unit |
| 5.8 | `WriteRouter` | Unit |
| 5.9 | Basic flow | Integration |
| 5.10 | Disconnection recovery | Integration |
| 5.11 | Write commands | Integration |
| 5.12 | Backward compatibility | Regression |
| 5.13 | Documentation | Documentation |

**Recommended order:** Write unit tests for each module as you implement it (Phases 1–4), then run integration tests once Phase 4 is complete. Backward compatibility verification is the final gate before declaring the refactor done.
