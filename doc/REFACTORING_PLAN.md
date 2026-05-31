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

**Responsibilities:**
- Single source of truth for ERD data
- Accessible from both appliance and MQTT sides
- Fire events when values change
- Clear publish flag after publishing (synchronous — no broker ACK callback available)

**Usage:**
- Appliance-side (subscription): writes updated ERD value → sets publish flag only if value changed
- Appliance-side (poll, `only_publish_on_change = true`): same — sets flag only if value changed
- Appliance-side (poll, `only_publish_on_change = false`): writes updated ERD value + unconditionally sets publish flag
- MQTT-side: reads flagged entries → publishes → clears flag immediately after publish call
- On MQTT reconnect: MQTT side iterates all flagged entries (flags persist while disconnected — no separate tracking needed)

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
- Clear publish flags synchronously after calling `publish()` (QoS=0; no broker ACK callback available)
- Route write commands to write queue

**Dependencies:**
- MQTT client
- ERD state table (read flagged entries, clear flags synchronously after publish)
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
- `MqttSideStateMachine` - manages MQTT lifecycle, publishes flagged ERDs, clears flags synchronously after publish

**Existing modules to refactor:**
- `ErdRegistry` - update to work with new ErdStateTable
- `AutodiscoveryManager` - finds appliance address (unchanged)
- `DeviceIdentityManager` - assembles device ID (unchanged)
- `FeatureBitManager` - validates ERDs (unchanged)
- `GeappliancesBridge` - orchestrates all modules

**Modules to ignore for now:**
- `HaDiscoveryManager` - defer to later phase

## Implementation Strategy

Each phase has a detailed implementation document with full interface specs, design rationale, and a completion checklist:

| Phase | Document | Summary |
|-------|----------|---------|
| 1 | [phase-1-foundation.md](phase-1-foundation.md) | Core data structures: `GlobalStateRegistry`, `ErdStateTable`, `WriteQueue` |
| 2 | [phase-2-appliance-side.md](phase-2-appliance-side.md) | Appliance-side handlers and `ApplianceSideStateMachine` |
| 3 | [phase-3-mqtt-side.md](phase-3-mqtt-side.md) | `WriteRouter`, `MqttSideStateMachine`; delete old C bridges |
| 4 | [phase-4-integration.md](phase-4-integration.md) | Wire FSMs into `GeappliancesBridge`; fix startup HSM |
| 5 | [phase-5-testing.md](phase-5-testing.md) | Unit, integration, and backward-compatibility tests |

The sections below are a concise summary of each phase. See the linked documents for full details.

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
   - Consumes ERD state table (read flagged entries, clears flags synchronously after each publish)
   - Handles MQTT connection lifecycle
   - Routes write commands to write queue
   - Clears publish flags synchronously after each `publish()` call (QoS=0; no ACK callback exists)

3. Update `EsphomeMqttClientAdapter`
   - Adapt to work with new state table structure
   - No ACK callback to hook — flag clearing is handled by `MqttSideStateMachine` synchronously
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
| **Push model (typed events)** | Modules react immediately to changes; easier to test than polling; decouples timing. Uses typed `tiny_event_t` per field — not string-keyed subscriptions — for compile-time safety. |
| **Isolated FSMs** | Each FSM owns its complexity; easier to reason about; can be tested independently |
| **ERD state table as source of truth** | Single point of truth; MQTT side doesn't need to know appliance protocol; appliance side doesn't care about MQTT |
| **Global state registry** | Read-only values like device ID and connection status are needed everywhere; centralized subscription avoids passing params through many layers |
| **Publish flags — `publish_flag` only** | `publish_flag` is both the change tracker and the reconnect-recovery mechanism. It stays set while MQTT is disconnected and is cleared synchronously after `publish()`. No separate `changed_since_last_publish` field needed. `only_publish_on_change` is enforced by the appliance side: SubscriptionHandler/PollingHandler call `set_publish_flag()` unconditionally or only on change, keeping the MQTT side simple. |
| **Fixed-size ERD value buffers** | Avoids per-ERD heap allocation and fragmentation on ESP32; ERD values are bounded at 255 bytes max; `MAX_ERD_VALUE_SIZE = 32` covers all known appliances |
| **Synchronous publish-flag clearing** | ESPHome MQTT publish is fire-and-forget at QoS=0; no broker ACK callback is available. Flags are cleared immediately after `publish()` returns. Reconnect resilience comes from the flag persisting until publish is *called*, not until it is *acknowledged*. |
| **GEA2 tight loop stays in GeappliancesBridge** | The 200 ms busy-loop is a hardware constraint that must run before any FSM loop() calls. Neither `ApplianceSideStateMachine` nor `MqttSideStateMachine` may own or replicate it. |

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| **Increased complexity during refactor** | Phase approach; keep old code working while adding new; run tests frequently |
| **GEA2 tight loop regression** | Preserve `run_protocol_stack_()` call order in `GeappliancesBridge::loop()` as first operation; add a regression test for GEA2 timing |
| **Event subscription overhead** | Lazy initialization; profiling to catch memory leaks; unsubscribe on module shutdown |
| **Reconnect edge cases** | Extensive testing of disconnect→reconnect scenarios; flag state must be durable across reconnect |
| **ErdEntry buffer overflow** | Assert or clamp in `update_erd_value()` if incoming value exceeds `MAX_ERD_VALUE_SIZE`; log a warning |

## Open Questions to Clarify Later

1. Should ERD state table persist values across reboots (e.g., via Flash storage)?
2. Multi-appliance support: should each appliance get its own state table, or one global table with appliance-ID keys?

---

## Codebase Analysis: Design Issues & Gaps

*This section documents issues found during review of the current implementation. Each issue must be resolved before or during the relevant implementation phase.*

### Critical Issues

#### 1. Dependency Ordering Bug: `WriteQueue` Belongs in Phase 1

`WriteHandler` (Todo 2.3) and `WriteRouter` (Todo 3.1) both depend on `WriteQueue`, but `WriteQueue` is currently placed in Phase 4 (Todo 4.1). This breaks the dependency chain — you cannot implement the handlers in Phase 2/3 without the queue they consume from/write to.

**Fix:** Move `WriteQueue` to Phase 1 as Todo 1.4, before all handler todos. (Already applied in the todos below.)

---

#### 2. MQTT Broker ACK Callbacks Are Not Available in ESPHome

`MqttSideStateMachine` proposes an `on_mqtt_ack(topic)` method to clear publish flags after broker acknowledgment. However, `esphome::mqtt::MQTTClientComponent::publish()` is fire-and-forget at QoS=0 with no per-message ACK callback. There is no hook available to know when the broker has confirmed receipt.

The current code already handles this correctly: `esphome_mqtt_client_adapter_drain_pending_updates()` calls `publish()` and removes the entry from the `pending_updates` map in the same call. Reconnect resilience is handled by the map persisting while disconnected.

**Fix:** Change "clear publish flag on broker ACK" to "clear publish flag immediately after calling `publish()`." Remove `on_mqtt_ack()` from the `MqttSideStateMachine` interface. Document that publish-flag clearing is synchronous — flags survive until publish is actually called, which is sufficient for reconnect recovery.

---

#### 3. GEA2 Tight Loop Is Incompatible with a Simple `loop()` Pattern

The plan proposes `ApplianceSideStateMachine::loop()` as a fast, non-blocking call. However, `GeappliancesBridge::run_protocol_stack_()` runs a **200 ms wall-clock busy loop** for GEA2 (required by the half-duplex 19200 baud protocol). This loop blocks the entire main task for its duration.

This is not a concern `ApplianceSideStateMachine` should own — it belongs at the `GeappliancesBridge` level where protocol selection (GEA2 vs GEA3) is known.

**Fix:** Explicitly document that `run_protocol_stack_()` remains in `GeappliancesBridge::loop()` and is called *before* both FSM `loop()` calls. `ApplianceSideStateMachine` and `MqttSideStateMachine` must not drive or duplicate the protocol stack.

---

### Important Issues

#### 4. Existing C-Based Bridges Are Not Addressed

`mqtt_bridge_t` (subscription mode) and `mqtt_bridge_polling_t` (polling mode) are existing C state machines that perform both appliance-side ERD reading AND MQTT publishing in one unit. The plan introduces `SubscriptionHandler`, `PollingHandler`, and `MqttSideStateMachine` without ever stating what happens to the existing bridges.

Without a clear decision, implementation risks duplicating logic or creating conflicting code paths.

**Fix:** State explicitly: `mqtt_bridge.c` and `mqtt_bridge_polling.c` will be **replaced** (not wrapped) by the new C++ handlers. Phase 2 extracts their appliance-side reading logic into `SubscriptionHandler`/`PollingHandler`. Phase 3 extracts their MQTT publishing logic into `MqttSideStateMachine`. The old files are deleted at the end of Phase 3.

---

#### 5. String-Keyed Subscriptions in `GlobalStateRegistry` Are Error-Prone

The plan proposes `subscribe_to(const std::string& key, Callback callback)`. String keys are:
- Prone to runtime typos that the compiler cannot catch
- More expensive than typed dispatch at runtime
- Inconsistent with the codebase's use of typed `tiny_event_t` / `tiny_event_subscription_t` pairs (see `on_write_request_event`, `on_mqtt_disconnect_event`, etc.)

**Fix:** Replace string-keyed subscriptions with typed per-field events — one `tiny_event_t` per subscribable field — or a C++ typed observer. For example:
```cpp
i_tiny_event_t* on_appliance_address_changed();
i_tiny_event_t* on_bridge_mode_changed();
```
This matches the idiom used throughout the existing codebase and catches subscriber typos at compile time.

---

#### 6. `std::vector<uint8_t>` per ERD Causes Heap Fragmentation

Storing ERD values as `std::vector<uint8_t>` allocates one heap object per ERD. With 100+ ERDs registered at runtime this creates significant heap fragmentation on ESP32. ERD values in this protocol are small (typically ≤ 10 bytes; absolute max 255 bytes).

**Fix:** Use a fixed-size inline buffer in `ErdEntry`:
```cpp
static constexpr uint8_t MAX_ERD_VALUE_SIZE = 32;
struct ErdEntry {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE];
  uint8_t value_size;
  bool publish_flag;
  uint32_t timestamp_ms;
};
```
If values larger than `MAX_ERD_VALUE_SIZE` are ever needed, document that as a separate concern.

---

#### 7. Thread Safety Scope Is Overstated

The plan adds mutex locks to both `GlobalStateRegistry` and `ErdStateTable` for "concurrent reads during normal operation." In practice, all logic in this project runs in the single-threaded ESPHome main loop task. The one cross-task boundary is the async MQTT publish path (`esphome_mqtt_client_adapter_publish()`), which already uses a FreeRTOS queue as its thread-crossing mechanism.

Adding full mutex locking to every ERD read/write in `ErdStateTable` and `GlobalStateRegistry` introduces latency and lock-contention risk for no benefit in a main-loop-only architecture.

**Fix:** Document the actual threading model: both `ErdStateTable` and `GlobalStateRegistry` are accessed exclusively from the ESPHome main loop task — no mutex needed. The FreeRTOS queue in `EsphomeMqttClientAdapter` remains the sole thread-crossing boundary and already handles its own synchronization.

---

### Minor Issues

#### 8. Static Global `g_bridge_services` Blocks Unit Testing

`geappliances_bridge_startup_hsm.cpp` holds a file-scope static `g_bridge_services` pointer set via `set_bridge_services()`. This prevents independent unit testing of the startup HSM — any test that instantiates the HSM will share state with any other test in the same binary.

**Fix:** Store the `IBridgeServices*` in the HSM's context field (or a wrapper struct) rather than as a file-scope static. This is a low-risk, contained change that should be part of the startup HSM refactor in Phase 4.

---

#### 9. `ErdRegistry` Refactoring Description Conflates Metadata with Values

Todo 1.3 says to refactor `ErdRegistry` to "read from both tables." But `ErdRegistry` tracks only metadata (valid ERDs, string-typed ERDs, registered ERDs). It has no concept of ERD values. There is no meaningful integration with `ErdStateTable` for metadata lookup.

**Fix:** `ErdRegistry` and `ErdStateTable` remain separate concerns. Rewrite Todo 1.3 as: *"Verify `ErdRegistry` works correctly alongside the new `ErdStateTable` — no structural changes needed. Confirm that `ErdStateTable` calls `ErdRegistry::is_valid()` before storing an ERD value (optional filtering layer)."*

---

#### 10. `HaDiscoveryManager` Integration Gap During Interim Phases

`HaDiscoveryManager` currently receives ERD-seen notifications via `on_ha_discovery_erd_seen_()` called from `GeappliancesBridge::handle_erd_client_activity_()`. In the target architecture it should subscribe to `ErdStateTable.on_erd_changed` events. But the plan defers this to a later phase without specifying the interim wiring.

**Fix:** Explicitly note: during Phases 2–3, `HaDiscoveryManager` continues to be driven by `handle_erd_client_activity_()` in `GeappliancesBridge` unchanged. Migration to `ErdStateTable` event subscriptions is a separate future todo, tracked separately.

---

#### 11. The Inline `MqttConnectionState` FSM Already Exists

The `MqttConnectionState` enum (DISCONNECTED → SUBSCRIBING → FLUSHING → RUNNING) in `GeappliancesBridge::loop()` is functionally the `MqttSideStateMachine` described in the plan. Phase 3 should acknowledge this and frame the work as **extracting** this existing inline FSM into a standalone class rather than writing it from scratch.

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

- Implement subscription mechanism using typed per-field events (not string keys — see Issue #5):
  - `i_tiny_event_t* on_appliance_address_changed()` - subscribe to address updates
  - `i_tiny_event_t* on_gea_protocol_type_changed()` - subscribe to protocol detection
  - `i_tiny_event_t* on_bridge_mode_changed()` - subscribe to mode transitions
  - (device_id is write-once; no subscription needed)

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

**Thread safety:** No mutex needed — all callers run in the ESPHome main loop task (see Issue #7).

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
// Fixed-size buffer avoids per-ERD heap allocation (see Issue #6).
static constexpr uint8_t MAX_ERD_VALUE_SIZE = 32;
struct ErdEntry {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE]; // Raw ERD data
  uint8_t value_size;
  bool publish_flag; // True = needs MQTT publish; persists while disconnected
};
```

**Methods to implement:**

*Write operations (appliance-side calls these):*
- `void update_erd_value(tiny_erd_t erd_id, const uint8_t* value, uint8_t size)`
  - Stores the new value
  - Sets `publish_flag = true` **only if the value differs** from the stored value
  - Fires `on_erd_changed` event
  - Used by SubscriptionHandler and PollingHandler (with `only_publish_on_change = true`)
- `void set_publish_flag(tiny_erd_t erd_id)`
  - Unconditionally sets `publish_flag = true` without changing the stored value
  - Used by PollingHandler when `only_publish_on_change = false` (called after `update_erd_value`)

*Read operations (MQTT-side calls these):*
- `std::vector<tiny_erd_t> get_flagged_erds() const` - return all ERDs with `publish_flag == true`
- `const uint8_t* get_erd_value(tiny_erd_t erd_id, uint8_t& size_out) const` - read current value
- `void clear_publish_flag(tiny_erd_t erd_id)` - called immediately after publish

*Query operations:*
- `bool has_flag(tiny_erd_t erd_id) const`

**Events:**
- `on_erd_changed(tiny_erd_t erd_id)` - fired when value updated with new data

**Thread safety:** No mutex needed — all callers run in the ESPHome main loop task (see Issue #7).

---

### Todo 1.3: Verify ErdRegistry Alongside ErdStateTable
**ID:** `verify-erd-registry`  
**Depends on:** `erd-state-table`  
**Expected output:** No new files; updated `erd_registry.h` only if a filtering hook is added

**Detailed Description:**
`ErdRegistry` and `ErdStateTable` are separate concerns and must remain so (see Issue #9):
- `ErdRegistry` — metadata only: which ERDs are valid, string-typed, registered at runtime
- `ErdStateTable` — runtime state only: current values, publish flags, timestamps

There is no structural integration needed. The one optional interaction is an early filter:
when `ErdStateTable::update_erd_value()` is called, it may consult `ErdRegistry::is_valid()` to
reject ERDs that are not in the valid set. This is an optimization, not a requirement.

**What to do:**
- Confirm existing `ErdRegistry` tests still pass (no changes expected)
- Optionally add a `ErdRegistry*` pointer to `ErdStateTable` for the `is_valid()` filter
- Document clearly that `ErdRegistry` does **not** store or read ERD values

**Test:** Run existing `make test` to confirm no regressions.

---

### Todo 1.4: Create WriteQueue
**ID:** `write-queue`  
**Depends on:** Nothing  
**Expected output:** `write_queue.h` and `write_queue.cpp`

**Detailed Description:**
Create a simple FIFO queue for write commands. This must exist before Phase 2 and Phase 3, since
both `WriteHandler` (Phase 2) and `WriteRouter` (Phase 3) depend on it. (Moved here from Phase 4 — see Issue #1.)

**What it should do:**
- FIFO queue of `WriteCommand` structs
- Bounded size (e.g., max 16 pending writes) to prevent unbounded memory use
- All access from the main ESPHome loop task (no mutex needed)
- Simple push/pop/peek interface

```cpp
struct WriteCommand {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE];
  uint8_t value_size;
  uint8_t appliance_address;
};

class WriteQueue {
public:
  bool push(const WriteCommand& cmd);  // Returns false if full
  bool pop(WriteCommand& cmd);         // Returns false if empty
  bool is_empty() const;
  size_t size() const;
};
```

---

## Phase 2: Appliance-Side Decoupling

### Todo 2.1: Create SubscriptionHandler Module
**ID:** `subscription-handler`  
**Depends on:** `erd-state-table`, `global-state-registry`  
**Expected output:** `subscription_handler.h` and `subscription_handler.cpp`

**Detailed Description:**
Extract the appliance-side subscription logic from `mqtt_bridge.c` into a standalone module that writes to the ERD state table. This **replaces** `mqtt_bridge.c` for the appliance side — `mqtt_bridge.c` owns both subscription *and* MQTT publishing; `SubscriptionHandler` owns only subscription. MQTT publishing moves to `MqttSideStateMachine` in Phase 3 (see Issue #4).

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
Extract the appliance-side polling logic from `mqtt_bridge_polling.c` into a standalone module that writes to the ERD state table. This **replaces** `mqtt_bridge_polling.c` for the appliance side — the existing polling bridge owns both polling *and* MQTT publishing; `PollingHandler` owns only polling. MQTT publishing moves to `MqttSideStateMachine` in Phase 3 (see Issue #4).

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
**Depends on:** `erd-state-table`, `global-state-registry`, `write-queue`  
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
// WriteCommand is defined in write_queue.h (fixed-size buffer, see Todo 1.4)

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
Extract and formalize the MQTT-side FSM. **Note:** This FSM already exists as the inline `MqttConnectionState` enum (DISCONNECTED → SUBSCRIBING → FLUSHING → RUNNING) in `GeappliancesBridge::loop()` (see Issue #11). Phase 3 extracts that existing logic into a standalone class — it is not written from scratch. Additionally, `mqtt_bridge.c` and `mqtt_bridge_polling.c` will be deleted after their MQTT-publishing logic is folded into this FSM.

**States (map directly from existing inline FSM):**
- **Disconnected** - no MQTT connection (replaces `MqttConnectionState::DISCONNECTED`)
- **Subscribing** - connected; registering wildcard write topic (replaces `SUBSCRIBING`)
- **Flushing** - subscribed; draining pending ERD updates (replaces `FLUSHING`)
- **Running** - steady-state; draining new ERD updates each loop (replaces `RUNNING`)

**Key behavior:**
- When MQTT connects, transition to Subscribing
- When wildcard write topic registered, transition to Flushing
- In Flushing/Running, call `erd_state_table->get_flagged_erds()`, publish each, and `clear_publish_flag()` **immediately after calling publish()** — ESPHome does not provide per-message broker ACK callbacks (QoS=0); clearing is synchronous (see Issue #2)
- On MQTT disconnect, transition to Disconnected; flags accumulate in state table
- On MQTT reconnect, resume from Subscribing; accumulated flags published

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
  // Note: no on_mqtt_ack() — ESPHome publish is fire-and-forget at QoS=0
};
```

---

## Phase 4: Integration & Orchestration

### Todo 4.1: Update GeappliancesBridge Orchestration
**ID:** `bridge-orchestration`  
**Depends on:** `appliance-fsm`, `mqtt-fsm`  
**Expected output:** Updated `geappliances_bridge.cpp` and `geappliances_bridge.h`

**Detailed Description:**
Refactor the main bridge class to initialize and drive both FSMs in the main loop. After this todo, `mqtt_bridge.c` and `mqtt_bridge_polling.c` are deleted (their logic has moved to the new handlers and `MqttSideStateMachine`).

**What needs to change:**
- Initialize `GlobalStateRegistry` and `ErdStateTable` in `setup()`
- Create appliance-side FSM, polling handler, subscription handler, write handler
- Create MQTT-side FSM, write router
- In `loop()`, preserve `run_protocol_stack_()` as the first call (before FSM loops) — the GEA2 200 ms tight loop must not move into any FSM (see Issue #3)
- Replace the inline `MqttConnectionState` FSM in `loop()` with `MqttSideStateMachine::loop()`
- Wire up event callbacks (MQTT connect/disconnect → FSM notifications)
- Delete `mqtt_bridge.c`, `mqtt_bridge_polling.c` and their headers

---

### Todo 4.2: Update StartupHsm for GlobalStateRegistry
**ID:** `startup-hsm-refactor`  
**Depends on:** `global-state-registry`  
**Expected output:** Updated `geappliances_bridge_startup_hsm.cpp`

**Detailed Description:**
Modify the startup FSM to populate the GlobalStateRegistry as it discovers appliance details. Also fix the static global `g_bridge_services` (see Issue #8).

**What needs to change:**
- After device ID is assembled → `registry->set_device_id(device_id)`
- After appliance address found → `registry->set_appliance_address(addr)`
- After GEA2/GEA3 detected → `registry->set_gea_protocol_type(type)`
- As startup progresses → `registry->set_bridge_mode(NEW_MODE)`
- Move `IBridgeServices*` from file-scope static `g_bridge_services` into the `tiny_hsm_t` context field (or a wrapper struct) to eliminate the global — this enables independent unit testing of the HSM

---

## Phase 5: Testing & Verification

### Todo 5.1: Unit Tests - GlobalStateRegistry
**ID:** `unit-tests-global-registry`  
**Depends on:** `global-state-registry`  
**Expected output:** `test/test_global_state_registry.cpp`

**Test cases:**
- Reads return correct values after set
- Subscriptions fire callbacks on value change
- Multiple subscribers for same field all called
- Unsubscribe prevents callback firing

---

### Todo 5.2: Unit Tests - ErdStateTable
**ID:** `unit-tests-erd-state`  
**Depends on:** `erd-state-table`  
**Expected output:** `test/test_erd_state_table.cpp`

**Test cases:**
- `update_erd_value()` with a new value sets `publish_flag = true`
- `update_erd_value()` with the same value leaves `publish_flag = false`
- `set_publish_flag()` sets flag unconditionally even when value unchanged
- `clear_publish_flag()` clears the flag after publishing
- `get_flagged_erds()` returns only flagged entries
- `on_erd_changed` fires only when value actually differs
- Values larger than `MAX_ERD_VALUE_SIZE` are rejected or truncated safely

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
- Poll response with changed value: `update_erd_value()` called → flag set
- Poll response with same value, `only_publish_on_change = true`: flag stays clear
- Poll response with same value, `only_publish_on_change = false`: `set_publish_flag()` called → flag set
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
- Flag cleared synchronously after `publish()` is called (no broker ACK)
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
6. Verify publish flag cleared synchronously after `publish()` returns (no ACK step needed)

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
