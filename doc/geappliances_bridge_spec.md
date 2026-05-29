# GeappliancesBridge Specification

This document defines the contractual requirements of the geappliances_bridge
ESPHome component. All future code changes must maintain these invariants
unless explicitly approved by the user.

---

## 1. Component Inventory

### 1.1 GeappliancesBridge (geappliances_bridge.h / .cpp)

**Role:** ESPHome component entry point. Owns all sub-components, drives the
startup HSM, runs the MQTT connection FSM in loop(), and implements
IBridgeServices.

**Owns:** All manager instances, adapter instances, bridge instances, ERD
clients, UART adapters, timer group, startup HSM, and the MQTT connection FSM.

**Dependencies:** ESPHome Component base class, ESPHome UART, ESPHome MQTT
global singleton, all sub-components listed below.

### 1.2 AutodiscoveryManager (autodiscovery_manager.h / .cpp)

**Role:** Locate the appliance on the GEA bus via broadcast discovery.
Alternates between GEA3 and GEA2 if both UARTs are configured. Retries
indefinitely.

**Owns:** Discovery state machine, retry counter, discovered host address,
active ERD client pointer, protocol type flag.

**Dependencies:** i_tiny_gea3_erd_client, i_tiny_gea2_erd_client,
gea2_erd_client_adapter (for GEA2-to-GEA3 translation).

**Initialization:** Called from setup(). Receives ERD client pointers and a
completion callback that sends signal_autodiscovery_complete to the startup HSM.

### 1.3 DeviceIdentityManager (device_identity_manager.h / .cpp)

**Role:** Read ERDs 0x0008 (appliance type), 0x0001 (model number), 0x0002
(serial number), then assemble a unique device ID string.

**Owns:** Sequential read state machine, raw ERD values, generated device ID
string, retry counter.

**Dependencies:** i_tiny_gea3_erd_client (the active ERD client from
autodiscovery).

**Initialization:** Called from the startup HSM device_id phase entry via
init_device_id_reading(). Receives the configured device ID (if any), the
active ERD client, and the host address.

**Note:** Does NOT hold an i_mqtt_client_t pointer. Does NOT publish ERD
values to MQTT. Does NOT write to the ERD publish table.

### 1.4 ErdRegistry (erd_registry.h / .cpp)

**Role:** Authoritative source for valid ERDs (from feature bits) and
string-type ERDs (from generated config). Registered ERDs are now derived
from the ERD publish table in the adapter, not tracked separately.

**Owns:** Two std::set<tiny_erd_t> instances (valid_erds_, string_erds_)
and a valid_erds_ready flag.

**Dependencies:** tiny_erd.h (type only).

**Initialization:** ErdRegistry starts empty. Valid ERDs are populated in
initialize_mqtt_bridge_() from FeatureBitManager results. String-type ERDs
are populated in initialize_mqtt_bridge_() from generated config. Registered
ERDs are no longer tracked here -- they are derived from the ERD publish
table in the adapter (see Section 5.1).

### 1.5 FeatureBitManager (feature_bit_manager.h / .cpp)

**Role:** Read 11 feature bit ERDs (0x0008, 0x0001, 0x0002, 0x0092-0x0097,
0x0109-0x010D), parse bitmasks into a valid ERD set.

**Owns:** Sequential read state machine, raw ERD data buffers, parsed valid
ERD set (std::set + std::vector), parse progress state.

**Dependencies:** i_tiny_gea3_erd_client, i_mqtt_client_t.

**Initialization:** Triggered when mqtt_client_adapter_initialized_ becomes
true and the ERD publish table is ready. The feature_bits phase starts
reading as soon as the adapter is initialized -- it does not wait for MQTT
connection. Receives the active ERD client, host address, i_mqtt_client_t
pointer, and mqtt_initialized flag.

**Note:** FeatureBitManager calls mqtt_client_update_erd() for every ERD
read response. This writes the ERD into the publish table in the adapter.
The adapter handles string conversion and deduplication.

### 1.6 EsphomeMqttClientAdapter (esphome_mqtt_client_adapter.h / .cpp)

**Role:** Implements i_mqtt_client_t for ESPHome. Owns the ERD publish table,
drains it with round-robin scheduling, subscribes to wildcard write topic,
routes write commands back via tiny_event.

**Owns:** ERD publish table (std::vector of ErdPublishEntry, max 400 entries),
drain index, wildcard_subscribed flag, mqtt_connected_at_ms timestamp, two
tiny_event instances (write request, disconnect), device_id string,
ErdRegistry pointer.

**Dependencies:** esphome::mqtt::global_mqtt_client (direct access),
ErdRegistry, tiny_event.

**Initialization:** Called from initialize_mqtt_client_(). Receives device ID
string and ErdRegistry pointer. Creates the ERD publish table (empty vector,
drain index = 0). The table is ready to accept entries immediately after init.

### 1.7 MqttBridge (mqtt_bridge.h / .cpp)

**Role:** Subscription mode. Subscribes to all ERDs at the appliance address,
forwards publications to i_mqtt_client_t, routes write commands back to the
ERD client.

**Owns:** tiny_hsm for subscription lifecycle, timer, event subscriptions.

**Dependencies:** i_tiny_gea3_erd_client, i_mqtt_client_t, tiny_hsm, tiny_timer.

**ESPHome coupling:** NONE. Pure C, ESPHome-independent.

### 1.8 MqttBridgePolling (mqtt_bridge_polling.h / .cpp)

**Role:** Polling mode. Iterates a polling list of ERDs, reads each
sequentially, publishes values to i_mqtt_client_t, routes write commands back.

**Owns:** tiny_hsm for polling lifecycle, timer, polling list, verification
list, event subscriptions.

**Dependencies:** i_tiny_gea3_erd_client, i_mqtt_client_t, tiny_hsm, tiny_timer.

**ESPHome coupling:** NONE. Pure C, ESPHome-independent.

### 1.9 HaDiscoveryManager (ha_discovery_manager.h / .cpp)

**Role:** Publish Home Assistant MQTT autodiscovery payloads. Fetches JSONL
entity definitions via HTTPS, parses them, publishes discovery messages with
rate limiting.

**Owns:** Discovery state machine, registered ERD snapshot, seen ERD set,
FreeRTOS task/queue/stack (ESP-IDF only), mqtt_adapter pointer.

**Dependencies:** esphome_mqtt_client_adapter_t (for async publish),
mqtt::MQTTClientComponent (for sync fallback), ESP-IDF esp_http_client,
ESP-IDF cJSON, FreeRTOS.

**Initialization:** Called from initialize_mqtt_bridge_(). Receives device
info, registered ERDs (from the adapter's publish table), and mqtt_adapter
pointer.

### 1.10 Startup HSM (geappliances_bridge_startup_hsm.h / .cpp)

**Role:** Drive the ordered startup phase sequence. Transitions between phases
based on signals from managers. Enforces timeouts.

**Owns:** tiny_hsm instance, back-pointer to IBridgeServices.

**Dependencies:** IBridgeServices interface, tiny_hsm, esphome::mqtt::
global_mqtt_client (for connection check in bridge_init phase only).

### 1.11 IBridgeServices (i_bridge_services.h)

**Role:** Abstract interface between the startup HSM and GeappliancesBridge.
Defines every operation the HSM may invoke.

**Dependencies:** bridge_mode.h (BridgeMode enum).

### 1.12 BridgeMode (bridge_mode.h)

**Role:** Enum with three values: POLL (0), SUBSCRIBE (1), AUTO (2).

**Dependencies:** None.

---

## 2. Startup Phase Contract

The startup HSM drives a linear sequence of 9 phases. Each phase has defined
entry conditions, per-loop work, and exit conditions.

### 2.1 Phase 1: protocol_stack

**Entry:** Always the initial state. Transitions immediately to autodiscovery.

**Per-loop work:** None (protocol stack is driven from run_protocol_stack_()
in loop(), which runs before the HSM).

**Exit:** Immediate transition to autodiscovery on entry.

### 2.2 Phase 2: autodiscovery

**Entry conditions:**
- AutodiscoveryManager has been initialized in setup()
- ERD client activity subscriptions are active

**Per-loop work:**
- run_autodiscovery() -- drives the broadcast discovery state machine
- ERD client activity events are routed to handle_erd_client_activity_(),
  which checks for autodiscovery responses

**Exit conditions:**
- AutodiscoveryManager reports is_complete() == true (a board responded)
- Transition to device_id

**Component state on exit:**
- autodiscovery_manager_.get_host_address() is valid
- autodiscovery_manager_.get_active_erd_client() is non-null
- autodiscovery_manager_.is_gea2_protocol() reflects the protocol type

**Note:** This phase retries indefinitely. There is no timeout.

### 2.3 Phase 3: device_id

**Entry conditions:**
- Autodiscovery is complete
- Host address and active ERD client are known

**Entry action:**
- init_device_id_reading() -- initializes DeviceIdentityManager with the
  configured device ID (if any), active ERD client, and host address

**Per-loop work:**
- run_device_id() -- drives the sequential ERD read state machine
- ERD client activity events are routed to handle_erd_client_activity_(),
  which delivers read_completed/read_failed to DeviceIdentityManager
- ERD read responses are NOT written to the publish table (device_id phase
  ERDs are consumed internally only)

**Exit conditions:**
- is_device_id_complete() == true (all three ERDs read successfully)
- signal_device_id_complete received from event routing
- Transition to mqtt_client_init

**Note:** There is NO timeout. The manager retries indefinitely until all
three ERDs (0x0008, 0x0001, 0x0002) are read successfully. is_device_id_failed()
and is_device_id_phase_timed_out() are not used as exit conditions.

**Component state on exit:**
- device_identity_manager_.get_device_id() returns a valid string
- device_identity_manager_.get_model_number() and get_serial_number() are valid

### 2.4 Phase 4: mqtt_client_init

**Entry conditions:**
- Device ID is known (read or pre-configured)

**Entry action:**
- initialize_mqtt_client() if not already initialized:
  - Calls esphome_mqtt_client_adapter_init() with the device ID
  - Creates the ERD publish table (empty vector, drain index = 0, max 400)
  - Sets ErdRegistry pointer on the adapter
  - Sets mqtt_client_adapter_initialized_ = true
- start_feature_bit_reading():
  - Calls FeatureBitManager::init() with active ERD client, host address,
    &mqtt_client_adapter_.interface, and mqtt_client_adapter_initialized_
- Transition to feature_bits

**Component state on exit:**
- mqtt_client_adapter_initialized_ == true
- ERD publish table exists and is empty, ready to accept entries
- ErdRegistry is empty (no string ERDs, no valid ERDs yet)
- FeatureBitManager is in READING_0008 state, ready to begin reads

### 2.5 Phase 5: feature_bits

**Entry conditions:**
- MQTT client adapter is initialized
- ERD publish table is created and ready to accept entries
- FeatureBitManager has been initialized with ERD client and mqtt_client

**Entry action:**
- None (feature bit reading begins immediately via run_feature_bits())

**Per-loop work:**
- run_feature_bits() -- drives the sequential ERD read state machine and
  incremental parsing
- ERD client activity events are routed to handle_erd_client_activity_(),
  which delivers read_completed/read_failed to FeatureBitManager
- FeatureBitManager calls mqtt_client_update_erd() for each ERD read response,
  writing the ERD into the publish table with mqtt_update_required = true

**Exit conditions:**
- is_feature_bits_complete() == true (all ERD reads have either succeeded
  or failed because the feature bit is not present on this appliance)
- signal_feature_bits_complete received from event routing
- Transition to bridge_init

**Note:** This phase is ENTIRELY DECOUPLED from MQTT connection status.
It does not check whether MQTT is connected. It completes when all 11
feature bit ERDs have been processed (read successfully or skipped because
not supported by the appliance). The MQTT connection gate belongs in
bridge_init (Section 2.6).

**Component state on exit:**
- feature_bit_manager_.get_valid_erds() contains the parsed ERD set
- feature_bit_manager_.is_valid_list_ready() == true
- ERD publish table contains entries for the feature bit ERDs that were read
- ErdRegistry valid_erds_ready is still false (not yet set)

### 2.6 Phase 6: bridge_init

**Entry conditions:**
- Feature bits are complete
- MQTT client adapter is initialized

**Entry action:**
- Log "Startup: Bridge init phase"

**Per-loop work / signal handling:**
- On signal_run_loop: check if bridge is not initialized AND autodiscovery
  is complete AND MQTT is connected. If all true, call initialize_mqtt_bridge().
- On signal_mqtt_connected: same check, allows immediate init if MQTT just
  connected.

**Note:** This phase is the GATE for MQTT connection. Feature bits may complete
while MQTT is not yet connected -- this phase waits until BOTH feature bits are
complete AND MQTT is connected before initializing the bridge.

**initialize_mqtt_bridge_() actions:**
1. Apply valid-ERD filter to ErdRegistry (from FeatureBitManager results)
2. Populate ErdRegistry string ERDs from generated config
3. Select operating mode (poll/subscribe/auto based on config and protocol)
4. Initialize the appropriate bridge:
   - Polling: mqtt_bridge_polling_init() with ERD client, mqtt_client
     adapter interface, polling interval, publish-on-change flag
   - Subscribe: mqtt_bridge_init() with ERD client, mqtt_client adapter
     interface, host address
5. Set mqtt_bridge_initialized_ = true
6. If generate_device_config: initialize HaDiscoveryManager with device info,
   registered ERDs (from the adapter's publish table), and mqtt_adapter pointer

**Exit:** Transition to subscription_watch

**Component state on exit:**
- mqtt_bridge_initialized_ == true
- Either subscription_bridge_initialized_ or polling_bridge_initialized_ is true
- ErdRegistry has valid ERDs set (filtering active)
- ErdRegistry has string ERDs populated
- HaDiscoveryManager is in WAITING_FOR_READY state (if enabled)
- ERD publish table contains entries from feature_bits phase + any new ERDs

### 2.7 Phase 7: subscription_watch

**Entry conditions:**
- Bridge is initialized
- MQTT is connected

**Per-loop work:**
- If AUTO mode and subscription_mode_active_: call check_subscription_activity()
  (10-second timeout, falls back to polling)
- call maybe_start_custom_erd_polling() (starts custom ERD polling alongside
  subscription bridge if conditions met)
- call log_poll_state_transitions()

**Exit conditions:**
- Non-AUTO mode: immediate transition to ha_discovery on entry
- AUTO mode: transition when subscription_mode_active_ becomes false
  (either confirmed subscription or fell back to polling)
- On signal_subscription_fallback: transition to ha_discovery

**Component state on exit:**
- Operating mode is confirmed (either subscription or polling)
- Custom ERD polling may be active alongside subscription bridge

### 2.8 Phase 8: ha_discovery

**Entry conditions:**
- Bridge is initialized
- Operating mode is confirmed

**Entry action:**
- Log "Startup: HA discovery phase"

**Per-loop work:**
- run_ha_discovery() -- drives the HaDiscoveryManager state machine

**Exit:** Immediate transition to running on first run() call

**Note:** The HSM does not wait for HA discovery to complete. It transitions
to running immediately, and HA discovery continues in the background via
the running state's recurring tasks.

### 2.9 Phase 9: running

**Entry conditions:**
- All startup phases complete
- Bridge is initialized and operating

**Entry action:**
- Log "Bridge is now in steady-state operation"

**Per-loop work:**
- run_all_managers() -- runs autodiscovery, device_identity, feature_bit
  managers (no-ops since they're complete)
- If AUTO mode and subscription_mode_active_: check_subscription_activity()
- maybe_start_custom_erd_polling()
- log_poll_state_transitions()
- run_ha_discovery()

**Exit:** None (terminal state)

---

## 3. Steady-State Contract

### 3.1 loop() Execution Order

Every call to GeappliancesBridge::loop() executes in this order:

1. MQTT Connection FSM (Section 4)
2. run_protocol_stack_() -- drives GEA2/GEA3 hardware (includes GEA2 tight
   loop if active, feeds TWDT)
3. esp_task_wdt_reset() (ESP32 only)
4. Startup HSM signal_run_loop

### 3.2 MQTT Connection FSM States

The MQTT FSM has four states, driven in loop() before the startup HSM:

**DISCONNECTED:**
- Entry: any state when mqtt_client->is_connected() becomes false
- Action: call esphome_mqtt_client_adapter_notify_disconnected()
- Exit: when mqtt_client->is_connected() becomes true, transition to SUBSCRIBING

**SUBSCRIBING:**
- Entry: connect edge detected
- Action: log "MQTT connected", send signal_mqtt_connected to startup HSM
- Gate: wait for mqtt_client_adapter_initialized_ to be true
- Exit: when adapter is initialized, call
  esphome_mqtt_client_adapter_subscribe_write_topic(), transition to FLUSHING

**FLUSHING:**
- Entry: subscribe completed
- Action: call esphome_mqtt_client_adapter_drain_pending_updates() each loop
- Exit: when drain finds no entries with mqtt_update_required = true,
  transition to RUNNING
- Fallback: if adapter not initialized, transition to RUNNING immediately

**RUNNING:**
- Entry: no pending updates
- Action: call esphome_mqtt_client_adapter_drain_pending_updates() each loop
  (drains any new updates from the publish table)
- Exit: on disconnect, transition to DISCONNECTED

### 3.3 ERD Value Flow

The ERD publish table is the single path from ERD data to MQTT:

1. Any ERD read response or subscription publication arrives
2. The source (FeatureBitManager, mqtt_bridge, or mqtt_bridge_polling) calls
   mqtt_client_update_erd(&mqtt_client_adapter_.interface, erd, data, size)
3. The adapter converts the raw bytes to a string (hex or ASCII based on
   ErdRegistry string-type detection), finds or creates the table entry for
   this ERD, updates the payload, and sets mqtt_update_required = true
4. The MQTT FSM drains up to 5 entries per loop iteration using round-robin
   scheduling (see Section 5.1)
5. Each drained entry is published to the MQTT broker and its
   mqtt_update_required is set to false

---

## 4. MQTT Connection Rules

### 4.1 Connection Detection

- Connection state is checked by reading mqtt::global_mqtt_client->is_connected()
  in the MQTT FSM at the top of loop()
- The MQTT FSM is the SINGLE authority on connection state for the component

### 4.2 Connect Edge

- When transitioning from DISCONNECTED to connected:
  - Log "MQTT connected"
  - Send signal_mqtt_connected to the startup HSM (may unblock bridge_init)
  - Transition FSM to SUBSCRIBING

### 4.3 Disconnect Edge

- When transitioning from any non-DISCONNECTED state to disconnected:
  - Transition FSM to DISCONNECTED
  - Call esphome_mqtt_client_adapter_notify_disconnected()
    (resets mqtt_connected_at_ms, publishes on_mqtt_disconnect event)
  - Do NOT call notify_disconnected() on reconnect -- only on genuine
    connection loss

### 4.4 Reconnect Behavior

- On reconnect, the FSM transitions: DISCONNECTED -> SUBSCRIBING -> FLUSHING
  -> RUNNING
- In SUBSCRIBING: the wildcard subscription is idempotent
  (wildcard_subscribed flag prevents re-subscribe). The adapter records
  mqtt_connected_at_ms on first call after reconnect.
- In FLUSHING: the drain resumes from the saved drain index, publishing
  entries that have mqtt_update_required = true. The drain index is NOT
  reset on reconnect -- it picks up where it left off.
- ESPHome's MQTT client automatically re-subscribes all registered topics on
  reconnect, so the wildcard write topic subscription persists.

### 4.5 Write Command Handling

- The wildcard subscription (geappliances/{device_id}/erd/+/write) delivers
  write commands to the adapter's lambda callback
- The callback parses the ERD from the topic, decodes hex payload, and
  publishes to the on_write_request_event
- Both mqtt_bridge and mqtt_bridge_polling subscribe to this event
- The active bridge routes the write to the ERD client

---

## 5. Data Flow Rules

### 5.1 ERD Publish Table

The ERD publish table is the core data structure of the EsphomeMqttClientAdapter.
It replaces the previous pending_updates map and the separate registered_erds
set.

**Structure:**

    std::vector<ErdPublishEntry> publish_table;  // max 400 entries
    size_t drain_index = 0;  // round-robin position

    struct ErdPublishEntry {
        tiny_erd_t erd;              // ERD identifier
        std::string payload;         // hex or ASCII string (converted at queue time)
        bool mqtt_update_required;   // true when payload has changed and needs publishing
    };

**Behavior:**

- The table starts empty after adapter init (mqtt_client_init phase).
- It begins accepting entries during the feature_bits phase.
- When mqtt_client_update_erd() is called:
  - The raw bytes are converted to a string (hex by default, ASCII if
    ErdRegistry marks the ERD as string-type). String conversion happens
    BEFORE the table lookup/insert.
  - The adapter searches the table for an existing entry with the same ERD.
    Uses a fast lookup (map from erd -> index, or linear scan for small tables).
  - If found: the payload is replaced and mqtt_update_required is set to true.
  - If not found and table size < 400: a new entry is appended to the vector
    with mqtt_update_required = true.
  - If not found and table size >= 400: the update is silently dropped.
- No filtering by valid ERDs occurs at the table level. Every ERD that
  arrives through mqtt_client_update_erd() is accepted. The valid-ERD
  filter is applied upstream (by the bridge or FeatureBitManager) before
  calling mqtt_client_update_erd().

**Drain (round-robin):**

- Called from the MQTT FSM in FLUSHING and RUNNING states.
- Starting at drain_index, scan forward through the table (wrapping to 0
  at the end).
- Publish up to 5 entries that have mqtt_update_required == true.
- For each published entry, set mqtt_update_required = false.
- After publishing 5 entries OR completing a full wrap-around (drain_index
  returns to where it started), stop.
- Update drain_index to the position after the last published entry (or
  the start of the next scan).
- Returns the count of entries remaining with mqtt_update_required == true.
  When 0, the FSM transitions from FLUSHING to RUNNING.

**Fairness guarantee:** Every ERD in the table gets a turn to publish,
regardless of how frequently it updates. A rapidly-changing ERD will not
starve slower ERDs. The tradeoff is that a hot ERD may wait up to one
full cycle through the table before its next update is published.

**Registered ERDs:** The set of "registered ERDs" (used by HaDiscoveryManager)
is derived from the publish table. Every ERD that has an entry in the table
is considered registered. There is no separate registered_erds set.

### 5.2 Write Result Publishing

- update_erd_write_result() publishes directly via global_mqtt_client,
  bypassing the publish table entirely.
- Write results are ephemeral (success/failure of a single write operation)
  and do not represent persistent state, so they do not belong in the table.
- If MQTT is disconnected, the write result is silently dropped.

### 5.3 HA Discovery Publishing

- HaDiscoveryManager uses esphome_mqtt_client_adapter_publish() for async
  publishing when mqtt_adapter_ is non-null.
- Falls back to mqtt_client->publish() directly when mqtt_adapter_ is null.
- Discovery messages are published with retain=true.
- HA discovery gets its registered ERD list from the adapter's publish table
  (all ERDs that have entries in the table).

### 5.4 Who Calls mqtt_client_update_erd()

- FeatureBitManager: calls for every ERD read response during the feature_bits
  phase (starting from mqtt_client_init when the table is ready)
- mqtt_bridge (subscription mode): calls on every ERD publication received
- mqtt_bridge_polling (polling mode): calls on every ERD read completed
- DeviceIdentityManager: does NOT call (device_id phase ERDs are consumed
  internally only, before the table exists)

---

## 6. Event Routing Rules

### 6.1 ERD Client Activity Events

Both GEA3 and GEA2 ERD clients publish activity events. Both are subscribed
to the same handler: GeappliancesBridge::handle_erd_client_activity_().

### 6.2 Routing Logic in handle_erd_client_activity_()

The function routes based on these conditions (in order):

1. **Subscription publications (mqtt_bridge_initialized_ && subscription type):**
   - Track AUTO mode activity detection
   - Reset HA discovery quiet window
   - Only processed when mqtt_bridge_initialized_ is true
   - The bridge handles mqtt_client_update_erd() internally

2. **Autodiscovery responses (in_gea3_discovery || in_gea2_discovery):**
   - Check for ERD_APPLIANCE_TYPE read_completed
   - Deliver to autodiscovery_manager_.on_broadcast_response()
   - Return immediately (do not route further)

3. **Pre-bridge reads (!mqtt_bridge_initialized_):**
   - read_completed: route to FeatureBitManager or DeviceIdentityManager
     based on should_route_to_feature_bits_()
   - read_failed: route similarly
   - FeatureBitManager calls mqtt_client_update_erd() for its ERD reads
   - DeviceIdentityManager does NOT call mqtt_client_update_erd()

4. **Post-bridge reads (mqtt_bridge_initialized_):**
   - The bridge's own event subscriptions handle these (the bridges subscribe
     directly to the ERD client activity event)

### 6.3 should_route_to_feature_bits_() Logic

Returns true when:
- FeatureBitManager is active (not complete, not failed, not parse_pending)
- AND the ERD is a feature bit ERD (0x0092-0x0097, 0x0109-0x010D)
- OR the ERD is a device info ERD (0x0008, 0x0001, 0x0002) AND
  DeviceIdentityManager is already complete

This ensures device info ERDs go to DeviceIdentityManager first, then to
FeatureBitManager once the device ID is resolved.

---

## 7. Component Lifecycle Rules

### 7.1 Initialization Order

1. setup() -- initializes timer group, autodiscovery manager, GEA3/GEA2
   components, ERD clients, event subscriptions
2. mqtt_client_init phase -- initializes MQTT adapter (creates ERD publish
   table, sets ErdRegistry pointer), starts FeatureBitManager
3. bridge_init phase -- applies valid ERD filter, populates string ERDs,
   initializes bridge, initializes HaDiscoveryManager

### 7.2 Destruction Order (teardown())

1. ha_discovery_manager_.cleanup() -- stops FreeRTOS task, frees resources
2. mqtt_bridge_destroy() if subscription_bridge_initialized_
3. mqtt_bridge_polling_destroy() if polling_bridge_initialized_
4. esphome_mqtt_client_adapter_destroy() if mqtt_client_adapter_initialized_
   (frees ERD publish table, device_id string, pending resources)
5. Component::teardown()

### 7.3 Re-initialization Rules

- The MQTT adapter is initialized exactly once (guarded by
  mqtt_client_adapter_initialized_). The ERD publish table is created at
  init time and persists for the lifetime of the adapter.
- The bridge is initialized exactly once (guarded by mqtt_bridge_initialized_)
- In AUTO mode fallback: the subscription bridge is destroyed and a polling
  bridge is created in its place (check_subscription_activity_)
- Custom ERD polling can be started alongside the subscription bridge
  (start_custom_erd_polling_)
- The ERD publish table is NOT cleared on bridge re-initialization. Entries
  persist across bridge mode changes.

### 7.4 Bridge Mode Selection

- GEA2 protocol: always polling (subscriptions not supported)
- BRIDGE_MODE_POLL: always polling
- BRIDGE_MODE_SUBSCRIBE: always subscription
- BRIDGE_MODE_AUTO: starts with subscription, falls back to polling after
  10 seconds with no subscription activity

---

## 8. Invariants

These conditions must always be true during normal operation:

### I1. mqtt_client_adapter_initialized_ guards all adapter calls
No function on mqtt_client_adapter_ is called unless
mqtt_client_adapter_initialized_ is true. The MQTT FSM checks this guard
before calling subscribe_write_topic() and drain_pending_updates().

### I2. mqtt_bridge_initialized_ guards bridge access
The polling bridge's polling_list_complete field is only accessed when
mqtt_bridge_initialized_ is true. The subscription activity tracking only
runs when mqtt_bridge_initialized_ is true.

### I3. ERD publish table is created before feature_bits starts
The ERD publish table exists and is ready to accept entries before the
feature_bits phase begins. FeatureBitManager will not start reading until
the table is created.

### I4. The startup HSM only uses IBridgeServices
The startup HSM state functions interact with the bridge exclusively through
the IBridgeServices interface. They never access GeappliancesBridge members
directly.

### I5. The bridges only see i_mqtt_client_t
mqtt_bridge and mqtt_bridge_polling receive i_mqtt_client_t* pointers. They
never see esphome_mqtt_client_adapter_t or any ESPHome types.

### I6. ERD client activity is always routed through handle_erd_client_activity_()
Both GEA3 and GEA2 ERD client activity events subscribe to the same handler.
No other code subscribes to these events at the component level (the bridges
subscribe independently after initialization).

### I7. The MQTT FSM runs before the startup HSM in loop()
The MQTT connection FSM is the first thing in loop(), before
run_protocol_stack_() and the startup HSM. This ensures connection state is
current when the HSM checks for MQTT connectivity.

### I8. notify_disconnected() is only called on genuine disconnect
The MQTT FSM only calls esphome_mqtt_client_adapter_notify_disconnected()
when transitioning from a non-DISCONNECTED state to DISCONNECTED. It does NOT
call it on reconnect (to avoid triggering GEA2 re-identification).

### I9. ERD publish table has a hard cap of 400 entries
If mqtt_client_update_erd() is called for an ERD not in the table and the
table already has 400 entries, the update is silently dropped. This bounds
memory usage.

### I10. Round-robin drain publishes up to 5 entries per call
The drain function scans from drain_index, publishes up to 5 entries with
mqtt_update_required == true, sets them to false, and advances drain_index.
It stops after 5 publishes or a full wrap-around, whichever comes first.

### I11. HaDiscoveryManager waits for a ready signal
In subscription mode: waits for HA_DISCOVERY_QUIET_MS (10s) of no new ERD
activity, with a HA_DISCOVERY_MAX_WAIT_MS (30s) safety cap.
In polling mode: waits for polling_list_complete.
Registered ERDs are derived from the publish table.

### I12. The bridge_init phase gates on MQTT connection
The bridge_init phase does not initialize the bridge until BOTH feature bits
are complete AND MQTT is connected. The feature_bits phase is entirely
decoupled from MQTT -- it completes whenever all ERD reads succeed or fail.
The MQTT connection gate belongs in bridge_init, not feature_bits.

---

## 9. Cross-Component Dependency Graph

```
GeappliancesBridge (owns everything)
  |
  +-- AutodiscoveryManager
  |     +-- i_tiny_gea3_erd_client_t (erd_client_)
  |     +-- i_tiny_gea2_erd_client_t (gea2_erd_client_)
  |     +-- gea2_erd_client_adapter_t (GEA2-to-GEA3 translation)
  |
  +-- DeviceIdentityManager
  |     +-- i_tiny_gea3_erd_client_t (active client from autodiscovery)
  |     (does NOT write to publish table)
  |
  +-- FeatureBitManager
  |     +-- i_tiny_gea3_erd_client_t (active client from autodiscovery)
  |     +-- i_mqtt_client_t (calls mqtt_client_update_erd() for each read)
  |
  +-- ErdRegistry
  |     +-- valid_erds_ (from FeatureBitManager, set in bridge_init)
  |     +-- string_erds_ (from generated config, set in bridge_init)
  |     (registered_erds derived from adapter publish table)
  |
  +-- EsphomeMqttClientAdapter
  |     +-- ERD publish table (vector<ErdPublishEntry>, max 400)
  |     +-- drain_index (round-robin position)
  |     +-- esphome::mqtt::global_mqtt_client (direct access)
  |     +-- ErdRegistry* (pointer, set in mqtt_client_init)
  |
  +-- mqtt_bridge (subscription mode)
  |     +-- i_tiny_gea3_erd_client_t
  |     +-- i_mqtt_client_t (&mqtt_client_adapter_.interface)
  |
  +-- mqtt_bridge_polling (polling mode)
  |     +-- i_tiny_gea3_erd_client_t
  |     +-- i_mqtt_client_t (&mqtt_client_adapter_.interface)
  |
  +-- HaDiscoveryManager
  |     +-- esphome_mqtt_client_adapter_t* (for async publish)
  |     +-- mqtt::MQTTClientComponent* (for sync fallback, passed via run())
  |     +-- registered ERDs from adapter publish table
  |
  +-- startup_hsm
        +-- IBridgeServices* (back-pointer to GeappliancesBridge)
        +-- mqtt::global_mqtt_client (for connection check in bridge_init)
```

---

## 10. Notes on Design Debts

These are known issues that the spec documents as current behavior. They are
NOT requirements -- they are observations of where the code stands.

### D1. HaDiscoveryManager depends on concrete ESPHome types
The run() method accepts mqtt::MQTTClientComponent*, and publish_next_entity_()
has a direct fallback to mqtt_client->publish(). This violates the abstraction
principle that MQTT operations should go through i_mqtt_client_t.

### D2. Startup HSM directly reads mqtt::global_mqtt_client in bridge_init
The bridge_init phase checks MQTT connectivity by reading
mqtt::global_mqtt_client->is_connected() directly, instead of delegating to
the adapter or the MQTT FSM.

### D3. update_erd_write_result() bypasses the publish table
Write results are published directly instead of going through the publish
table. This is intentional -- write results are ephemeral and do not
represent persistent state. However, they are silently dropped on disconnect.

### D4. ErdRegistry registered_erds is now derived from the publish table
The ErdRegistry no longer maintains a separate registered_erds set. Instead,
HaDiscoveryManager reads registered ERDs from the adapter's publish table.
This means HA discovery can only discover ERDs that have been seen by the
adapter (which is correct -- you can't discover an ERD you haven't seen).
