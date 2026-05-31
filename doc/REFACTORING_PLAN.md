# Adapter Refactoring: Decoupling Appliance and MQTT Sides

## Problem Statement
The current adapter architecture couples the appliance-side (subscription/polling) and MQTT-side code tightly. Modules call each other directly, making the codebase harder to test in isolation and limits flexibility. The goal is to decouple these concerns using a shared state model and isolated state machines that communicate only through well-defined interfaces.

## Proposed Approach
Create a **two-tier decoupling** system:

1. **ERD State Table** - shared data store between appliance and MQTT sides
2. **Global State Registry** - subscribable global variables (device ID, connection status, appliance address, etc.)
3. **Two Independent State Machines** - appliance-side and MQTT-side, each isolated with minimal scope
4. **Push-based Event Model** - state changes trigger subscribed modules to react

## Architecture

### Tier 1: ERD State Table
A centralized registry of all ERDs with metadata:
- **ERD ID** - unique identifier
- **Current Value** - latest data from appliance or subscription
- **Publish Flag** - indicates data needs to be published to MQTT
- **Timestamp** - when the value last changed
- **Change Tracking** - whether value has changed since last publish

**Responsibilities:**
- Single source of truth for ERD data
- Accessible from both appliance and MQTT sides
- Thread-safe reads/writes (or atomic operations)
- Fire events when values change
- Clear publish flag on MQTT broker ACK (event-driven)

**Usage:**
- Appliance-side: writes updated ERD values → sets publish flag
- MQTT-side: reads flagged entries → publishes → clears flag when MQTT broker ACKs
- On MQTT reconnect: MQTT side iterates flagged entries (those that changed during disconnect)

### Tier 2: Global State Registry
Subscribable global variables for bridge-level state:
- Device ID (read-only after generation)
- Appliance address (board ID discovered)
- GEA2/GEA3 protocol detected
- Current bridge mode (startup/running/error)

**Responsibilities:**
- Modules can read current values
- Modules can subscribe to change events
- Event-driven state propagation

**Usage:**
- MQTT side reads device ID when needed
- Appliance-side state machine tracks its own state via registry
- Modules react to bridge mode/protocol changes

### Tier 3: Two Isolated State Machines

#### **Appliance-Side State Machine**
Manages communication with the GEA3 appliance. Coordinates subscription, polling, and write handlers.

**States:**
- `Idle` - waiting for appliance discovery
- `Running` - actively managing subscription/polling/writes via delegated handlers
- `Error` - recovery needed

**Delegated Handlers (active in Running state):**
- **SubscriptionHandler** - subscribes to configured ERDs, pushes updates to ERD state table (can be disabled)
- **PollingHandler** - polls configured ERDs on a timer, pushes updates to ERD state table (can be disabled)
- **WriteHandler** - reads write queue, sends writes to appliance

Configuration determines which handlers are active (subscription-only, polling-only, or both).

**Actions:**
- Transition to Running when appliance is discovered
- Delegate to handlers based on configuration
- Update ERD state table via handlers
- Process write commands from write queue

**Dependencies:**
- GEA3 ERD client
- ERD state table (write)
- Global state registry (read appliance address)
- Write queue (read)

#### **MQTT-Side State Machine**
Publishes ERD updates to MQTT broker and routes write commands to appliance.

**States:**
- `Idle` - waiting for appliance side
- `Connecting` - attempting to connect to broker
- `Subscribing` - setting up write-command subscriptions
- `Running` - continuously checking for flagged ERDs to publish and processing write commands

**Actions:**
- Connect to MQTT broker
- Subscribe to write-command topics
- Check ERD state table for flagged entries
- Publish flagged ERDs (via MQTT client)
- Clear publish flags when MQTT broker ACKs (event-driven, no timer blocking)
- Route write commands to write queue

**Dependencies:**
- MQTT client
- ERD state table (read flagged entries, write/clear flags on ACK)
- Global state registry (read device ID)
- Write queue (write)

### Tier 4: Module Interfaces
Small, focused modules with limited scope, easy to test:

**Appliance-Side Modules (new):**
- `SubscriptionHandler` - subscribes to configured ERDs, writes updates to ERD state table
- `PollingHandler` - polls configured ERDs on timer, writes updates to ERD state table
- `WriteHandler` - reads write queue, sends writes to appliance
- `ApplianceSideStateMachine` - coordinates the above handlers

**MQTT-Side Modules (new):**
- `WriteRouter` - routes write commands to write queue
- `MqttSideStateMachine` - manages MQTT lifecycle, publishes flagged ERDs, clears flags on ACK

**Existing modules to refactor:**
- `ErdRegistry` - update to work with new ErdStateTable
- `AutodiscoveryManager` - finds appliance address (unchanged)
- `DeviceIdentityManager` - assembles device ID (unchanged)
- `FeatureBitManager` - validates ERDs (unchanged)
- `GeappliancesBridge` - orchestrates all modules

**Modules to ignore for now:**
- `HaDiscoveryManager` - defer to later phase

## Implementation Strategy

### Phase 1: Foundation (Global State & ERD State Table)
1. Create `GlobalStateRegistry` class
   - Store global variables (device ID, MQTT status, appliance address, etc.)
   - Implement subscription/event mechanism for changes
   - Provide read-only and write interfaces

2. Create `ErdStateTable` class
   - Store ERD ID → {value, publish_flag, timestamp, change_tracked}
   - Thread-safe operations
   - Fire events on value changes
   - Methods to query flagged entries, clear flags, update values

3. Refactor `ErdRegistry` to read from both tables
   - Integrate with new ErdStateTable for state reads
   - Keep validation logic unchanged

### Phase 2: Decouple Appliance Side
1. Create separate **SubscriptionHandler**, **PollingHandler**, and **WriteHandler** modules
   - SubscriptionHandler: subscribes to configured ERDs, writes updates to ERD state table
   - PollingHandler: polls configured ERDs on timer, writes updates to ERD state table
   - WriteHandler: reads write queue, sends writes to appliance via GEA3 client
   - Configuration determines which handlers are active

2. Extract appliance-side logic into `ApplianceSideStateMachine`
   - Manage overall appliance connection state (Idle → Running → Error)
   - Delegate to active handlers in Running state
   - Handle recovery and reconnection

3. Update `EsphomeUartAdapter` and `Gea2ErdClientAdapter`
   - Minimal changes; remain pure I/O adapters
   - Still abstract UART/GEA2 details for appliance-side handlers

### Phase 3: Decouple MQTT Side
1. Create **WriteRouter** module
   - Reads write commands from MQTT subscriptions
   - Routes to write queue for appliance-side to process
   - Decouples MQTT-side from appliance implementation

2. Extract MQTT logic into `MqttSideStateMachine`
   - Consumes ERD state table (read flagged entries, clear flags on ACK)
   - Handles MQTT connection lifecycle
   - Routes write commands to write queue
   - Clears publish flags when MQTT broker ACKs (event-driven, non-blocking)

3. Update `EsphomeMqttClientAdapter`
   - Adapt to work with new state table structure
   - Hook MQTT broker ACK callback to trigger flag-clear events
   - Remove direct appliance integration

### Phase 4: Startup & Coordination
1. Refactor `GeappliancesBridge` to orchestrate new modules
   - Initialize ERD state table and global registry
   - Construct and wire new state machines
   - Drive both FSMs in the main loop

2. Update `StartupHsm` to initialize global state registry
   - Set device ID when generated
   - Set appliance address when discovered
   - Update bridge mode as startup progresses

3. Add module-to-module coordination via subscriptions
   - MQTT side subscribes to "appliance connected" events
   - Appliance side subscribes to "MQTT connected" events for write processing
   - HaDiscoveryManager subscribes to "appliance ready" events

### Phase 5: Testing & Integration
1. Write unit tests for each new module in isolation
   - Test ERD state table: value updates, flagging, event firing
   - Test global state registry: subscriptions, change events
   - Test appliance-side FSM with mock GEA3 client
   - Test MQTT-side FSM with mock MQTT client

2. Integration tests
   - Full appliance discovery → MQTT publish flow
   - Reconnection scenarios (appliance drops, reconnects)
   - Write commands flowing from MQTT → appliance

3. Verify existing functionality preserved
   - Polling still works
   - Subscriptions still work
   - HA discovery still publishes correctly
   - Device ID generation unchanged

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Push model (events)** | Modules react immediately to changes; easier to test than polling; decouples timing |
| **Isolated FSMs** | Each FSM owns its complexity; easier to reason about; can be tested independently |
| **ERD state table as source of truth** | Single point of truth; MQTT side doesn't need to know appliance protocol; appliance side doesn't care about MQTT |
| **Global state registry** | Read-only values like device ID and connection status are needed everywhere; centralized subscription avoids passing params through many layers |
| **Publish flags instead of queues** | Bounds memory (one flag per ERD); simpler recovery on reconnect; deduplication built-in |

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| **Increased complexity during refactor** | Phase approach; keep old code working while adding new; run tests frequently |
| **Thread safety of shared state** | Use atomic operations or locks where needed; document synchronization assumptions |
| **Event subscription overhead** | Lazy initialization; profiling to catch memory leaks; unsubscribe on module shutdown |
| **Reconnect edge cases** | Extensive testing of disconnect→reconnect scenarios; flag state must be durable |

## Open Questions to Clarify Later

1. Should ERD state table persist values across reboots (e.g., via Flash storage)?
2. Multi-appliance support: should each appliance get its own state table, or one global table with appliance-ID keys?

---

---

# Implementation Todos

## Phase 1: Foundation (Core Data Structures)

### Todo 1.1: Create GlobalStateRegistry Class
**ID:** `global-state-registry`  
**Depends on:** Nothing  
**Expected output:** `global_state_registry.h` and `global_state_registry.cpp`

**Detailed Description:**
Create a C++ class that manages subscribable global state variables accessible to all modules. This is the bridge between the StartupHsm (which sets the values) and the FSMs (which read them).

**What it should do:**
- Store four read-only values (after initialization):
  - `device_id` (std::string) - set by StartupHsm after assembly
  - `appliance_address` (uint8_t) - set by AutodiscoveryManager
  - `gea_protocol_type` (uint8_t or enum) - set when GEA2 or GEA3 detected
  - `bridge_mode` (enum) - set as startup progresses (STARTING → RUNNING → ERROR)

- Implement subscription mechanism:
  - `subscribe_to(const std::string& key, Callback callback)` - register for change notifications
  - `unsubscribe_from(const std::string& key)` - unregister callback
  - `notify_subscribers(const std::string& key)` - call all subscribed callbacks

- Implement read accessors:
  - `const std::string& get_device_id() const`
  - `uint8_t get_appliance_address() const`
  - `uint8_t get_gea_protocol_type() const`
  - `BridgeMode get_bridge_mode() const`

- Implement write methods (used only during initialization):
  - `void set_device_id(const std::string& id)`
  - `void set_appliance_address(uint8_t addr)` - triggers notify
  - `void set_gea_protocol_type(uint8_t type)` - triggers notify
  - `void set_bridge_mode(BridgeMode mode)` - triggers notify

**Thread safety:** Use mutex locks for concurrent reads during normal operation (multiple modules may read simultaneously)

**Testing notes:** This will need unit tests for subscription/notification behavior

---

### Todo 1.2: Create ErdStateTable Class
**ID:** `erd-state-table`  
**Depends on:** Nothing  
**Expected output:** `erd_state_table.h` and `erd_state_table.cpp`

**Detailed Description:**
Create a C++ class that manages all ERD state. This is the core shared data structure between appliance-side and MQTT-side FSMs.

**Data structure (per ERD):**
```cpp
struct ErdEntry {
  tiny_erd_t erd_id;
  std::vector<uint8_t> value;      // Raw ERD data
  bool publish_flag;               // True = needs MQTT publish
  uint32_t timestamp_ms;           // When value last changed (millis())
  bool changed_since_last_publish;  // Track if changed during disconnect
};
```

**Methods to implement:**

*Write operations (appliance-side calls these):*
- `void update_erd_value(tiny_erd_t erd_id, const std::vector<uint8_t>& value)`
  - If value differs from stored, set `publish_flag = true`
  - Update timestamp
  - Fire `on_erd_changed` event

*Read operations (MQTT-side calls these):*
- `std::vector<tiny_erd_t> get_flagged_erds() const` - return all ERDs with `publish_flag == true`
- `const std::vector<uint8_t>& get_erd_value(tiny_erd_t erd_id) const` - read current value
- `void clear_publish_flag(tiny_erd_t erd_id)` - called after MQTT broker ACKs

*Query operations:*
- `bool has_flag(tiny_erd_t erd_id) const`
- `uint32_t get_timestamp(tiny_erd_t erd_id) const`

**Events:**
- `on_erd_changed(tiny_erd_t erd_id)` - fired when value updated with new data

**Thread safety:** Use mutex for all access (both appliance-side and MQTT-side may access concurrently)

---

### Todo 1.3: Refactor ErdRegistry Integration
**ID:** `refactor-erd-registry`  
**Depends on:** `erd-state-table`  
**Expected output:** Updated `erd_registry.cpp` to use new ErdStateTable

**Detailed Description:**
Update the existing `ErdRegistry` class to integrate with the new `ErdStateTable` while maintaining its current interface and responsibilities.

**Current responsibilities (keep these):**
- Store valid-ERD set (filtered from FeatureBitManager)
- Store string-type ERD set (from generated ha_string_erd_ids[])
- Track registered ERDs at runtime

**New integration:**
- Add a pointer to the `ErdStateTable` (injected during initialization)
- When methods like `is_valid()`, `is_string_type()` are called, they may now query the state table
- No breaking changes to existing public interface

**Test:** Ensure existing tests still pass after refactoring

---

## Phase 2: Appliance-Side Decoupling

### Todo 2.1: Create SubscriptionHandler Module
**ID:** `subscription-handler`  
**Depends on:** `erd-state-table`, `global-state-registry`  
**Expected output:** `subscription_handler.h` and `subscription_handler.cpp`

**Detailed Description:**
Extract subscription logic from current `mqtt_bridge.c` into a standalone module that updates the ERD state table.

**What it should do:**
- Maintain subscription state (which ERDs are subscribed)
- Use GEA3 ERD client to subscribe to a configured set of ERDs
- When subscription updates arrive, call `erd_state_table->update_erd_value()`
- Handle subscription lifecycle (start, stop, error recovery)
- Do NOT call MQTT client directly

**Public interface:**
```cpp
class SubscriptionHandler {
public:
  SubscriptionHandler(i_tiny_gea3_erd_client_t* erd_client, 
                      ErdStateTable* state_table);
  
  void subscribe_to_erds(const std::vector<tiny_erd_t>& erd_list);
  void unsubscribe_all();
  void handle_erd_update(tiny_erd_t erd_id, const tiny_erd_value_t& value);
  
  bool is_subscribed_to(tiny_erd_t erd_id) const;
};
```

---

### Todo 2.2: Create PollingHandler Module
**ID:** `polling-handler`  
**Depends on:** `erd-state-table`, `global-state-registry`  
**Expected output:** `polling_handler.h` and `polling_handler.cpp`

**Detailed Description:**
Extract polling logic from current `mqtt_bridge_polling.c` into a standalone module that updates the ERD state table.

**What it should do:**
- Maintain polling state (timer, which ERDs are polled)
- Use GEA3 ERD client to poll a configured set of ERDs on an interval
- When poll responses arrive, call `erd_state_table->update_erd_value()`
- Handle polling errors and retry logic
- Do NOT call MQTT client directly

**Public interface:**
```cpp
class PollingHandler {
public:
  PollingHandler(i_tiny_gea3_erd_client_t* erd_client, 
                 ErdStateTable* state_table,
                 uint32_t polling_interval_ms);
  
  void set_polling_interval(uint32_t ms);
  void add_erd_to_poll(tiny_erd_t erd_id);
  void remove_erd_from_poll(tiny_erd_t erd_id);
  void poll_now();  // Trigger immediate poll
  void handle_poll_response(tiny_erd_t erd_id, const tiny_erd_value_t& value);
};
```

---

### Todo 2.3: Create WriteHandler Module
**ID:** `write-handler`  
**Depends on:** `erd-state-table`, `global-state-registry`  
**Expected output:** `write_handler.h` and `write_handler.cpp`

**Detailed Description:**
Create a new module that consumes write commands from a write queue and sends them to the appliance.

**What it should do:**
- Monitor a write queue for pending write commands
- For each write, use GEA3 ERD client to write the value to appliance
- Clear the write from the queue after successful send (or after N retries)
- Log/handle write failures

**Public interface:**
```cpp
struct WriteCommand {
  tiny_erd_t erd_id;
  std::vector<uint8_t> value;
  uint8_t appliance_address;
};

class WriteHandler {
public:
  WriteHandler(i_tiny_gea3_erd_client_t* erd_client,
               WriteQueue* write_queue);
  
  void process_writes();  // Called from FSM loop
  void on_write_response(tiny_erd_t erd_id, bool success);
};
```

---

### Todo 2.4: Create ApplianceSideStateMachine
**ID:** `appliance-fsm`  
**Depends on:** `erd-state-table`, `global-state-registry`  
**Expected output:** `appliance_side_state_machine.h` and `appliance_side_state_machine.cpp`

**Detailed Description:**
Create the appliance-side FSM that coordinates SubscriptionHandler, PollingHandler, and WriteHandler based on configuration.

**States:**
- **Idle** - waiting for appliance discovery (appliance_address == 0)
- **Running** - actively delegating to handlers (subscription/polling/write)
- **Error** - recovery needed (subscription/polling failed, will retry)

**State transitions:**
- Idle → Running: when `appliance_address` set (via GlobalStateRegistry subscription)
- Running → Error: when handlers report repeated failures
- Error → Running: after retry delay

**What handlers are active (configuration-driven):**
```cpp
struct ApplianceSideConfig {
  bool enable_subscriptions;
  bool enable_polling;
  uint32_t polling_interval_ms;
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
  void loop();  // Called from main bridge loop
  State get_current_state() const;
};
```

---

## Phase 3: MQTT-Side Decoupling

### Todo 3.1: Create WriteRouter Module
**ID:** `write-router`  
**Depends on:** (no FSM dependencies)  
**Expected output:** `write_router.h` and `write_router.cpp`

**Detailed Description:**
Create a module that routes MQTT write commands to a write queue.

**What it should do:**
- Subscribe to MQTT wildcard write topic: `geappliances/{device_id}/erd/+/write`
- Parse write commands from MQTT payload
- Push to write queue for appliance-side to process
- Handle write acknowledgments back to MQTT side

**Public interface:**
```cpp
class WriteRouter {
public:
  WriteRouter(i_mqtt_client_t* mqtt_client,
              WriteQueue* write_queue,
              const std::string& device_id);
  
  void subscribe_to_write_topic();
  void on_write_received(const std::string& topic, const std::string& payload);
};
```

---

### Todo 3.2: Create MqttSideStateMachine
**ID:** `mqtt-fsm`  
**Depends on:** `erd-state-table`, `global-state-registry`  
**Expected output:** `mqtt_side_state_machine.h` and `mqtt_side_state_machine.cpp`

**Detailed Description:**
Create the MQTT-side FSM that manages publishing ERD updates and handling broker ACKs.

**States:**
- **Idle** - waiting for MQTT client initialization
- **Connecting** - attempting to connect to broker
- **Subscribing** - setting up write-command subscriptions
- **Running** - continuously:
  - Check for flagged ERDs in state table
  - Publish them to MQTT
  - Wait for broker ACK event
  - Clear flag when ACK received

**Key behavior:**
- When MQTT connects, transition to Subscribing
- When subscriptions set up, transition to Running
- In Running, loop through `erd_state_table->get_flagged_erds()`
- For each flagged ERD:
  1. Publish to `geappliances/{device_id}/erd/{erd_id}/value`
  2. Wait for MQTT broker ACK callback
  3. Call `erd_state_table->clear_publish_flag(erd_id)`
- On MQTT disconnect, stay in Running but no publishes (flags accumulate)
- On MQTT reconnect, resume publishing accumulated flags

**Public interface:**
```cpp
class MqttSideStateMachine {
public:
  MqttSideStateMachine(ErdStateTable* state_table,
                       GlobalStateRegistry* registry,
                       i_mqtt_client_t* mqtt_client,
                       WriteRouter* write_router);
  
  void loop();  // Called from main bridge loop
  State get_current_state() const;
  void on_mqtt_connected();
  void on_mqtt_disconnected();
  void on_mqtt_ack(const std::string& topic);  // Called by MQTT adapter
};
```

---

## Phase 4: Integration & Orchestration

### Todo 4.1: Create WriteQueue
**ID:** `write-queue`  
**Depends on:** Nothing  
**Expected output:** `write_queue.h` and `write_queue.cpp`

**Detailed Description:**
Create a simple thread-safe queue for write commands (appliance-side consumes, MQTT-side produces).

**What it should do:**
- FIFO queue of WriteCommand structs
- Thread-safe push/pop
- Simple, similar to current pending-updates pattern

---

### Todo 4.2: Update GeappliancesBridge Orchestration
**ID:** `bridge-orchestration`  
**Depends on:** `appliance-fsm`, `mqtt-fsm`  
**Expected output:** Updated `geappliances_bridge.cpp` and `geappliances_bridge.h`

**Detailed Description:**
Refactor the main bridge class to initialize and drive both FSMs in the main loop.

**What needs to change:**
- Initialize `GlobalStateRegistry` and `ErdStateTable` in `setup()`
- Create appliance-side FSM, polling handler, subscription handler, write handler
- Create MQTT-side FSM, write router
- In `loop()`, call both FSMs' `loop()` methods
- Wire up event callbacks (MQTT connect/disconnect → FSM notifications)

---

### Todo 4.3: Update StartupHsm for GlobalStateRegistry
**ID:** `startup-hsm-refactor`  
**Depends on:** `global-state-registry`  
**Expected output:** Updated `geappliances_bridge_startup_hsm.cpp`

**Detailed Description:**
Modify the startup FSM to populate the GlobalStateRegistry as it discovers appliance details.

**What needs to change:**
- After device ID is assembled → `registry->set_device_id(device_id)`
- After appliance address found → `registry->set_appliance_address(addr)`
- After GEA2/GEA3 detected → `registry->set_gea_protocol_type(type)`
- As startup progresses → `registry->set_bridge_mode(NEW_MODE)`

---

## Phase 5: Testing & Verification

### Todo 5.1: Unit Tests - GlobalStateRegistry
**ID:** `unit-tests-global-registry`  
**Depends on:** `global-state-registry`  
**Expected output:** `test/test_global_state_registry.cpp`

**Test cases:**
- Reads return correct values after set
- Subscriptions fire callbacks on value change
- Multiple subscribers for same key all called
- Unsubscribe prevents callback firing
- Thread safety: concurrent reads don't corrupt state

---

### Todo 5.2: Unit Tests - ErdStateTable
**ID:** `unit-tests-erd-state`  
**Depends on:** `erd-state-table`  
**Expected output:** `test/test_erd_state_table.cpp`

**Test cases:**
- Value updates trigger change event
- Publish flag set when new value written
- Publish flag cleared via `clear_publish_flag()`
- `get_flagged_erds()` returns only flagged entries
- Thread safety: concurrent reads/writes don't corrupt

---

### Todo 5.3: Unit Tests - SubscriptionHandler
**ID:** `unit-tests-subscription-handler`  
**Depends on:** `subscription-handler`  
**Expected output:** `test/test_subscription_handler.cpp`

**Test cases:**
- Subscribe to ERD list and verify subscription calls
- Handle subscription update → state table update
- Unsubscribe removes ERDs from subscription
- Error handling on failed subscription

---

### Todo 5.4: Unit Tests - PollingHandler
**ID:** `unit-tests-polling-handler`  
**Depends on:** `polling-handler`  
**Expected output:** `test/test_polling_handler.cpp`

**Test cases:**
- Poll interval triggers poll
- Poll response updates state table
- Can add/remove ERDs from poll list
- Poll retries on failure

---

### Todo 5.5: Unit Tests - WriteHandler
**ID:** `unit-tests-write-handler`  
**Depends on:** `write-handler`  
**Expected output:** `test/test_write_handler.cpp`

**Test cases:**
- Write command from queue sent to appliance
- Write response clears from queue
- Failed write retried N times then dropped
- Multiple writes processed in order

---

### Todo 5.6: Unit Tests - ApplianceSideStateMachine
**ID:** `unit-tests-appliance-fsm`  
**Depends on:** `appliance-fsm`  
**Expected output:** `test/test_appliance_side_fsm.cpp`

**Test cases:**
- Idle → Running when appliance_address set
- Running → Error on handler failures
- Error → Running after retry delay
- Both subscription and polling modes work
- Write handler processes queue in Running state

---

### Todo 5.7: Unit Tests - MqttSideStateMachine
**ID:** `unit-tests-mqtt-fsm`  
**Depends on:** `mqtt-fsm`  
**Expected output:** `test/test_mqtt_side_fsm.cpp`

**Test cases:**
- Idle → Connecting on MQTT trigger
- Connecting → Subscribing when connected
- Subscribing → Running when subscribed
- Running publishes flagged ERDs
- Flag cleared when broker ACK received
- Disconnection pauses publishing, reconnect resumes
- Accumulated flags published on reconnect

---

### Todo 5.8: Unit Tests - WriteRouter
**ID:** `unit-tests-write-router`  
**Depends on:** `write-router`  
**Expected output:** `test/test_write_router.cpp`

**Test cases:**
- MQTT write message parsed and queued
- Invalid write format handled gracefully
- Multiple writes queued in order

---

### Todo 5.9: Integration Test - Basic Flow
**ID:** `integration-test-basic`  
**Depends on:** `appliance-fsm`, `mqtt-fsm`  
**Expected output:** `test/test_integration_basic.cpp`

**Test scenario:**
1. Start with appliance discovered (address set)
2. Verify appliance FSM transitions to Running
3. Simulate ERD subscription update
4. Verify state table updated with value
5. Verify MQTT FSM publishes to broker
6. Simulate broker ACK
7. Verify publish flag cleared

---

### Todo 5.10: Integration Test - Disconnection Recovery
**ID:** `integration-test-disconnect`  
**Depends on:** `appliance-fsm`, `mqtt-fsm`  
**Expected output:** `test/test_integration_disconnect.cpp`

**Test scenarios:**
- MQTT disconnect: flags accumulate but don't publish
- MQTT reconnect: all accumulated flags published
- Appliance disconnect: appliance FSM errors, retries
- Appliance reconnect: subscription/polling resumes

---

### Todo 5.11: Integration Test - Write Commands
**ID:** `integration-test-writes`  
**Depends on:** `write-handler`, `write-router`  
**Expected output:** `test/test_integration_writes.cpp`

**Test scenario:**
1. MQTT write received on write topic
2. WriteRouter queues command
3. WriteHandler consumes from queue
4. Command sent to appliance
5. Appliance responds with success
6. Queue cleared

---

### Todo 5.12: Backward Compatibility Verification
**ID:** `backward-compat-verification`  
**Depends on:** All integration tests  
**Expected output:** Test report

**What to verify:**
- Run existing test suite (`make test` and `make pytest`)
- Existing functionality still works:
  - Polling mode
  - Subscription mode
  - Mixed mode
  - Device ID generation
  - ERD registry validation
- No regressions in performance or memory usage

---

### Todo 5.13: Update Architecture Documentation
**ID:** `documentation-update`  
**Depends on:** All previous todos  
**Expected output:** Updated `doc/` files with architecture diagrams and module descriptions

**What to document:**
- Data flow diagrams (appliance side, MQTT side, integration)
- Module responsibilities and interfaces
- State machine state diagrams
- Event flow (subscriptions, callbacks)
- Configuration options
- Migration guide (if any old code remains)

---

## Summary Table

| Phase | Todo Count | Critical Path | Estimated Complexity |
|-------|-----------|----------------|----------------------|
| **1. Foundation** | 3 | Yes | Medium |
| **2. Appliance** | 4 | Yes | Medium-High |
| **3. MQTT** | 3 | Yes | Medium-High |
| **4. Integration** | 3 | Yes | Medium |
| **5. Testing** | 13 | Verify | High |
| **TOTAL** | 26 | | |

## Critical Dependency Path

```
Phase 1:
  global-state-registry ─┐
  erd-state-table ────────┼── refactor-erd-registry
                          ┘

Phase 2:
  erd-state-table ─┬─ subscription-handler ─┐
                   ├─ polling-handler ───────┼─ appliance-fsm
                   ├─ write-handler ────────┘
                   global-state-registry ──┘

Phase 3:
  erd-state-table ─┬─ mqtt-fsm
  global-state-registry ─┘
  write-queue ────────── write-router

Phase 4:
  appliance-fsm ──┬─ bridge-orchestration ── startup-hsm-refactor
  mqtt-fsm ───────┘

Phase 5:
  (all unit tests can run in parallel once their dependencies ready)
  (integration tests depend on bridge-orchestration)
  (backward-compat depends on all integration tests)
  (documentation depends on everything)
```

## Implementation Notes

1. **Keep old code working while adding new:** Don't delete old files until new code replaces them. This allows phased integration.

2. **Use dependency injection:** FSMs should receive pointers/references to dependencies (state table, registry, MQTT client) rather than creating them. Makes testing easier.

3. **Event-driven, not polling:** Use callbacks and events rather than checking flags in loops. Reduces CPU usage, cleaner code.

4. **Thread safety throughout:** Mark all shared data access with mutex comments. Use atomics where appropriate.

5. **Run tests frequently:** After each phase, run unit tests for that phase before moving to the next.

6. **Consider this a long-term investment:** Better architecture now saves debugging time later when adding features (HA Discovery, multi-appliance, etc.).
