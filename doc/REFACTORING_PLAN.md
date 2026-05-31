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

# Implementation Todos

## Phase 1: Foundation
- [ ] `global-state-registry` - Creating GlobalStateRegistry class
  - Implement subscribable global variables for device ID, MQTT status, appliance address, protocol detection, and bridge mode. Support subscription events so modules can react to state changes.
  - **Status:** pending

- [ ] `erd-state-table` - Creating ErdStateTable class
  - Implement centralized ERD state store with: ERD ID, value, publish flag, timestamp, change tracking. Thread-safe operations. Fire events on value changes. Methods to query/clear flagged entries.
  - **Status:** pending

- [ ] `refactor-erd-registry` - Refactoring ErdRegistry integration
  - Update ErdRegistry to read from new ErdStateTable for state queries while keeping validation logic. Ensure all existing methods still work.
  - **Depends on:** erd-state-table
  - **Status:** pending

## Phase 2: Appliance-Side Decoupling
- [ ] `appliance-fsm` - Extracting ApplianceSideStateMachine
  - Extract subscription and polling logic from mqtt_bridge.c and mqtt_bridge_polling.c. Create HSM for states: Idle → Subscribing → Running → Error. Write only to ERD state table.
  - **Depends on:** erd-state-table, global-state-registry
  - **Status:** pending

- [ ] `uart-adapter-refactor` - Refactoring UART/GEA2 adapters
  - Update EsphomeUartAdapter and Gea2ErdClientAdapter to be pure I/O adapters without side effects on MQTT state.
  - **Depends on:** appliance-fsm
  - **Status:** pending

- [ ] `decouple-appliance-mqtt` - Removing direct appliance→MQTT calls
  - Eliminate direct dependencies between appliance-side and MQTT-side code. All communication goes through ERD state table.
  - **Depends on:** appliance-fsm, mqtt-fsm
  - **Status:** pending

## Phase 3: MQTT-Side Decoupling
- [ ] `mqtt-fsm` - Extracting MqttSideStateMachine
  - Create HSM for MQTT publishing. States: Idle → Connecting → Subscribing → Publishing → Running. Reads flagged ERDs from state table, publishes, clears flags.
  - **Depends on:** erd-state-table, global-state-registry, appliance-fsm
  - **Status:** pending

- [ ] `mqtt-adapter-refactor` - Refactoring EsphomeMqttClientAdapter
  - Update MQTT client adapter to work with new state table structure. Keep pending-update deduplication logic. Remove direct appliance integration.
  - **Depends on:** mqtt-fsm
  - **Status:** pending

- [ ] `write-command-handling` - Decoupling write-command routing
  - Design write-command flow: MQTT side detects writes → queues to state table entry → appliance side processes. No direct coupling.
  - **Depends on:** decouple-appliance-mqtt
  - **Status:** pending

## Phase 4: Startup & Coordination
- [ ] `startup-hsm-refactor` - Updating StartupHsm for global registry
  - Modify StartupHsm to populate GlobalStateRegistry: device ID, appliance address, protocol type, bridge mode. Coordinate with new FSMs.
  - **Depends on:** global-state-registry
  - **Status:** pending

- [ ] `bridge-orchestration` - Updating GeappliancesBridge orchestration
  - Refactor GeappliancesBridge to initialize and drive both new state machines. Wire modules together via new interfaces.
  - **Depends on:** appliance-fsm, mqtt-fsm
  - **Status:** pending

- [ ] `module-subscriptions` - Adding module-to-module coordination
  - Wire up subscriptions: MQTT side listens for "appliance connected", appliance side for "MQTT connected", HaDiscoveryManager for "appliance ready".
  - **Depends on:** bridge-orchestration
  - **Status:** pending

## Phase 5: Testing & Integration
- [ ] `unit-tests-global-registry` - Writing GlobalStateRegistry unit tests
  - Test subscriptions, change events, concurrent reads, state consistency. Mock subscriber behavior.
  - **Depends on:** global-state-registry
  - **Status:** pending

- [ ] `unit-tests-erd-state` - Writing ErdStateTable unit tests
  - Test value updates, flag transitions, event firing, concurrent access, change tracking.
  - **Depends on:** erd-state-table
  - **Status:** pending

- [ ] `unit-tests-appliance-fsm` - Writing ApplianceSideStateMachine tests
  - Test state transitions, subscription lifecycle, polling, write handling with mock GEA3 client. Verify ERD state table updates.
  - **Depends on:** appliance-fsm
  - **Status:** pending

- [ ] `unit-tests-mqtt-fsm` - Writing MqttSideStateMachine tests
  - Test state transitions, reconnect recovery, flag clearing, write routing with mock MQTT client.
  - **Depends on:** mqtt-fsm
  - **Status:** pending

- [ ] `integration-tests-discovery` - Integration test: appliance discovery→publish
  - Test full flow: appliance discovery → MQTT connect → subscribe → publish discovered ERDs.
  - **Depends on:** module-subscriptions
  - **Status:** pending

- [ ] `integration-tests-reconnect` - Integration test: reconnection scenarios
  - Test appliance disconnect/reconnect, MQTT disconnect/reconnect, flag state recovery.
  - **Depends on:** module-subscriptions
  - **Status:** pending

- [ ] `integration-tests-writes` - Integration test: write command flow
  - Test write commands flowing MQTT → appliance via decoupled architecture.
  - **Depends on:** write-command-handling
  - **Status:** pending

- [ ] `backward-compat-verification` - Verifying backward compatibility
  - Run existing test suite. Verify polling, subscriptions, HA discovery, device ID generation all unchanged.
  - **Depends on:** integration-tests-discovery, integration-tests-reconnect, integration-tests-writes
  - **Status:** pending

- [ ] `documentation-update` - Updating architecture documentation
  - Document new modules, interfaces, state machines, and how data flows through the system.
  - **Depends on:** backward-compat-verification
  - **Status:** pending

## Summary

**Total Todos:** 21
- Foundation Phase: 3 todos
- Appliance-Side Phase: 3 todos
- MQTT-Side Phase: 3 todos
- Startup & Coordination Phase: 3 todos
- Testing & Integration Phase: 9 todos

**Critical Path (longest dependency chain):**
1. Create GlobalStateRegistry
2. Create ErdStateTable
3. Create ApplianceSideStateMachine
4. Create MqttSideStateMachine
5. Bridge Orchestration
6. Module Subscriptions
7. Integration Tests
8. Backward Compatibility Verification
