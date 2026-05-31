# Phase 1: Foundation — Core Data Structures

## Purpose

Build the shared data structures that all other phases depend on. Nothing can be implemented in Phases 2–5 until this phase is complete. All four todos in this phase have no dependencies on existing bridge code and can be developed and tested in isolation.

---

## Prerequisites

- Familiarity with the existing `tiny_event_t` / `tiny_event_subscription_t` idiom used throughout the codebase
- Understanding of `tiny_erd_t` (the ERD identifier type)
- No existing modules need to change in this phase

---

## Key Design Decisions for This Phase

### No mutex needed
All code in this project runs in the single ESPHome main loop task. The only cross-task boundary is `esphome_mqtt_client_adapter_publish()`, which already uses a FreeRTOS queue internally. **Do not add mutexes to `ErdStateTable` or `GlobalStateRegistry`.**

### Typed per-field events (not string keys)
`GlobalStateRegistry` uses one `tiny_event_t` per subscribable field — not a `subscribe_to(std::string& key, ...)` API. String keys cause runtime typos the compiler cannot catch and are inconsistent with the rest of the codebase.

### Fixed-size ERD value buffers
`ErdEntry` stores values in a `uint8_t value[MAX_ERD_VALUE_SIZE]` inline buffer (not `std::vector<uint8_t>`). This avoids per-ERD heap allocation on the ESP32. `MAX_ERD_VALUE_SIZE = 32` covers all known appliances; the protocol max is 255 bytes.

### `publish_flag` semantics
`publish_flag` is the sole mechanism for tracking what needs to be sent to MQTT. It serves two roles:
1. **Change tracking** — set only when the value actually changes (default)
2. **Reconnect recovery** — persists while MQTT is disconnected; cleared synchronously immediately after `publish()` is called (ESPHome MQTT is QoS=0, no broker ACK callback exists)

`ErdStateTable` exposes two write methods to cover both appliance-side use cases:
- `update_erd_value()` — sets flag only if value changed
- `set_publish_flag()` — sets flag unconditionally (used by PollingHandler when `only_publish_on_change = false`)

### `ErdRegistry` and `ErdStateTable` are separate concerns
`ErdRegistry` is metadata-only (valid ERD set, string-typed ERDs, registered ERDs). It has no concept of runtime values. `ErdStateTable` is runtime state only (values, flags). They do not structurally integrate — the only optional interaction is `ErdStateTable` checking `ErdRegistry::is_valid()` before storing a value.

---

## Todos

### Todo 1.1: Create `GlobalStateRegistry`
**ID:** `global-state-registry`
**Depends on:** Nothing
**Output:** `components/geappliances_bridge/global_state_registry.h` and `.cpp`

Create a C++ class that manages subscribable global state variables accessible to all modules. The StartupHsm sets these values; the FSMs read them.

**Values to store:**
- `device_id` (std::string) — set once by StartupHsm after assembly; write-once, no subscription needed
- `appliance_address` (uint8_t) — set by AutodiscoveryManager when found
- `gea_protocol_type` (uint8_t or enum) — set when GEA2 or GEA3 detected
- `bridge_mode` (enum: `STARTING`, `RUNNING`, `ERROR`) — updated as startup progresses

**Read accessors:**
```cpp
const std::string& get_device_id() const;
uint8_t get_appliance_address() const;
uint8_t get_gea_protocol_type() const;
BridgeMode get_bridge_mode() const;
```

**Write methods** (called only during startup/initialization):
```cpp
void set_device_id(const std::string& id);        // write-once; no notify
void set_appliance_address(uint8_t addr);          // notifies subscribers
void set_gea_protocol_type(uint8_t type);          // notifies subscribers
void set_bridge_mode(BridgeMode mode);             // notifies subscribers
```

**Event accessors** (typed per-field, not string-keyed):
```cpp
i_tiny_event_t* on_appliance_address_changed();
i_tiny_event_t* on_gea_protocol_type_changed();
i_tiny_event_t* on_bridge_mode_changed();
```

---

### Todo 1.2: Create `ErdStateTable`
**ID:** `erd-state-table`
**Depends on:** Nothing
**Output:** `components/geappliances_bridge/erd_state_table.h` and `.cpp`

The core shared data structure between appliance-side and MQTT-side. Stores current ERD values and publish flags.

**Data structure:**
```cpp
static constexpr uint8_t MAX_ERD_VALUE_SIZE = 32;

struct ErdEntry {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE]; // raw ERD data; fixed-size to avoid heap alloc
  uint8_t value_size;
  bool publish_flag; // true = needs MQTT publish; persists while MQTT disconnected
};
```

**Write operations** (called by appliance-side handlers):
```cpp
// Stores value; sets publish_flag=true ONLY if value differs from stored.
// Fires on_erd_changed event.
// Used by: SubscriptionHandler, PollingHandler (only_publish_on_change=true)
void update_erd_value(tiny_erd_t erd_id, const uint8_t* value, uint8_t size);

// Unconditionally sets publish_flag=true without changing the stored value.
// Used by: PollingHandler when only_publish_on_change=false (call after update_erd_value)
void set_publish_flag(tiny_erd_t erd_id);
```

**Read operations** (called by MQTT-side FSM):
```cpp
// Returns all ERDs with publish_flag==true
std::vector<tiny_erd_t> get_flagged_erds() const;

// Returns pointer to stored value and its size; nullptr if ERD not found
const uint8_t* get_erd_value(tiny_erd_t erd_id, uint8_t& size_out) const;

// Called immediately after publish() — synchronous, no broker ACK needed
void clear_publish_flag(tiny_erd_t erd_id);
```

**Query:**
```cpp
bool has_flag(tiny_erd_t erd_id) const;
```

**Events:**
```cpp
// Fired when update_erd_value() stores a value that differs from what was stored
i_tiny_event_t* on_erd_changed(); // args: tiny_erd_t erd_id
```

**Safety:** Assert or clamp if incoming `size > MAX_ERD_VALUE_SIZE`; log a warning.

---

### Todo 1.3: Verify `ErdRegistry` Alongside `ErdStateTable`
**ID:** `verify-erd-registry`
**Depends on:** `erd-state-table`
**Output:** No new files; minor update to `erd_registry.h` only if filtering hook is added

`ErdRegistry` and `ErdStateTable` are separate concerns and must remain so:
- `ErdRegistry` — metadata only: which ERDs are valid, string-typed, registered at runtime
- `ErdStateTable` — runtime state only: current values, publish flags

**What to do:**
1. Confirm existing `ErdRegistry` tests still pass unchanged (`make test`)
2. Optionally pass an `ErdRegistry*` to `ErdStateTable` so `update_erd_value()` can call `ErdRegistry::is_valid()` to reject unknown ERDs (optional guard, not required for correctness)
3. Add a comment to both headers clarifying their distinct roles

---

### Todo 1.4: Create `WriteQueue`
**ID:** `write-queue`
**Depends on:** Nothing
**Output:** `components/geappliances_bridge/write_queue.h` and `.cpp`

A simple bounded FIFO for write commands. Must exist before Phase 2 (`WriteHandler`) and Phase 3 (`WriteRouter`) can be implemented.

```cpp
struct WriteCommand {
  tiny_erd_t erd_id;
  uint8_t value[MAX_ERD_VALUE_SIZE]; // MAX_ERD_VALUE_SIZE defined in erd_state_table.h
  uint8_t value_size;
  uint8_t appliance_address;
};

class WriteQueue {
public:
  bool push(const WriteCommand& cmd); // returns false if full
  bool pop(WriteCommand& cmd_out);    // returns false if empty
  bool is_empty() const;
  size_t size() const;

  static constexpr size_t MAX_PENDING_WRITES = 16;
};
```

All access is from the ESPHome main loop task — no mutex needed.

---

## Phase Completion Checklist

- [ ] `global_state_registry.h/.cpp` compiles cleanly
- [ ] `erd_state_table.h/.cpp` compiles cleanly
- [ ] `write_queue.h/.cpp` compiles cleanly
- [ ] Existing `ErdRegistry` tests still pass: `make test`
- [ ] Unit tests written for `GlobalStateRegistry` (see Phase 5 Todo 5.1)
- [ ] Unit tests written for `ErdStateTable` (see Phase 5 Todo 5.2)
- [ ] No `std::vector` heap allocations per ERD in `ErdEntry`
- [ ] No mutex added to `ErdStateTable` or `GlobalStateRegistry`

---

## What This Unlocks

After Phase 1 is complete:
- **Phase 2** can create `SubscriptionHandler`, `PollingHandler`, and `WriteHandler`, all of which write to `ErdStateTable` and read from `WriteQueue`
- **Phase 3** can create `WriteRouter` (writes to `WriteQueue`) and `MqttSideStateMachine` (reads from `ErdStateTable`)
- Phase 2 and Phase 3 can proceed largely in parallel once Phase 1 is done
