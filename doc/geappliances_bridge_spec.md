# GeappliancesBridge Specification

This document defines the current behavior and contractual requirements of the
geappliances_bridge ESPHome component. All future code changes must maintain
these invariants unless explicitly approved by the user.

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
values to MQTT.

### 1.4 ErdRegistry (erd_registry.h / .cpp)

**Role:** Single authoritative source for three ERD sets: valid ERDs (from
feature bits), string-type ERDs (from generated config), and registered ERDs
(tracked at runtime by the MQTT adapter).

**Owns:** Three std::set<tiny_erd_t> instances and a valid_erds_ready flag.

**Dependencies:** tiny_erd.h (type only).

**Initialization:** String ERDs populated in initialize_mqtt_client_(). Valid
ERDs populated in initialize_mqtt_bridge_() from FeatureBitManager results.
Registered ERDs appended by the MQTT adapter on each register_erd() call.

### 1.5 FeatureBitManager (feature_bit_manager.h / .cpp)

**Role:** Read 11 feature bit ERDs (0x0008, 0x0001, 0x0002, 0x0092-0x0097,
0x0109-0x010D), parse bitmasks into a valid ERD set.

**Owns:** Sequential read state machine, raw ERD data buffers, parsed valid
ERD set (std::set + std::vector), parse progress state.

**Dependencies:** i_tiny_gea3_erd_client, i_mqtt_client_t (received in init()
but currently NOT used for publishing -- see Section 5.2).

**Initialization:** Called from the startup HSM mqtt_client_init phase via
start_feature_bit_reading_. Receives the active ERD client, host address,
i_mqtt_client_t pointer, and mqtt_initialized flag.

**Note:** The i_mqtt_client_t pointer is stored but NOT used for publishing
during the feature_bits phase. The code explicitly avoids calling
mqtt_client_update_erd() for feature bit ERDs to prevent heap pressure on
ESP32-C3 (see feature_bit_manager.cpp lines 149-157).

### 1.6 EsphomeMqttClientAdapter (esphome_mqtt_client_adapter.h / .cpp)

**Role:** Implements i_mqtt_client_t for ESPHome. Queues ERD updates for
async publish, deduplicates by ERD, subscribes to wildcard write topic,
routes write commands back via tiny_event.

**Owns:** Pending updates map (keyed by ERD), wildcard_subscribed flag,
mqtt_connected_at_ms timestamp, two tiny_event instances (write request,
disconnect), device_id string, ErdRegistry pointer.

**Dependencies:** esphome::mqtt::global_mqtt_client (direct access),
ErdRegistry, tiny_event.

**Initialization:** Called from initialize_mqtt_client_(). Receives device ID
string and ErdRegistry pointer.

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
info, registered ERD set, and mqtt_adapter pointer.

### 1.10 Startup HSM (geappliances_bridge_startup_hsm.h / .cpp)

**Role:** Drive the ordered startup phase sequence. Transitions between phases
based on signals from managers. Enforces timeouts.

**Owns:** tiny_hsm instance, back-pointer to IBridgeServices.

**Dependencies:** IBridgeServices interface, tiny_hsm, esphome::mqtt::
global_mqtt_client (for connection checks in feature_bits and bridge_init
phases).

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
- record_device_id_phase_start() -- starts the 30-second timeout timer

**Per-loop work:**
- Check phase timeout (30 seconds). If exceeded, transition to mqtt_client_init.
- run_device_id() -- drives the sequential ERD read state machine
- ERD client activity events are routed to handle_erd_client_activity_(),
  which delivers read_completed/read_failed to DeviceIdentityManager

**Exit conditions (any of):**
- is_device_id_complete() == true (all three ERDs read successfully)
- is_device_id_failed() == true (should not occur with indefinite retry)
- is_device_id_phase_timed_out() == true (30-second timeout)
- signal_device_id_complete or signal_device_id_failed received from event
  routing
- Transition to mqtt_client_init

**Component state on exit:**
- device_identity_manager_.get_device_id() returns a valid string
- device_identity_manager_.get_model_number() and get_serial_number() are valid

### 2.4 Phase 4: mqtt_client_init

**Entry conditions:**
- Device ID is known (read or pre-configured)

**Entry action:**
- initialize_mqtt_client() if not already initialized:
  - Calls esphome_mqtt_client_adapter_init() with the device ID
  - Populates ErdRegistry string ERDs from generated config
  - Sets ErdRegistry pointer on the adapter
  - Sets mqtt_client_adapter_initialized_ = true
- start_feature_bit_reading():
  - Calls FeatureBitManager::init() with active ERD client, host address,
    &mqtt_client_adapter_.interface, and mqtt_client_adapter_initialized_
- Transition to feature_bits

**Component state on exit:**
- mqtt_client_adapter_initialized_ == true
- ErdRegistry has string ERDs populated, valid ERDs empty
- FeatureBitManager is in READING_0008 state, ready to begin reads

### 2.5 Phase 5: feature_bits

**Entry conditions:**
- MQTT client adapter is initialized
- FeatureBitManager has been initialized with ERD client and mqtt_client

**Entry action:**
- record_feature_bits_phase_start() -- starts the 60-second timeout timer

**Per-loop work:**
- Check phase timeout (60 seconds). If exceeded, call mark_feature_bits_timed_out().
- run_feature_bits() -- drives the sequential ERD read state machine and
  incremental parsing
- ERD client activity events are routed to handle_erd_client_activity_(),
  which delivers read_completed/read_failed to FeatureBitManager

**Exit conditions:**
- is_feature_bits_complete() == true AND MQTT is connected
  (mqtt::global_mqtt_client != nullptr && is_connected())
- OR: signal_feature_bits_complete received AND MQTT is already connected
- OR: signal_mqtt_connected received AND feature bits are already complete
- Transition to bridge_init

**Component state on exit:**
- feature_bit_manager_.get_valid_erds() contains the parsed ERD set
- feature_bit_manager_.is_valid_list_ready() == true
- ErdRegistry valid_erds_ready is still false (not yet set)

**Note:** The phase gates on BOTH feature bits completion AND MQTT connection.
If feature bits complete before MQTT connects, the phase waits. If MQTT
connects before feature bits complete, the phase waits.

### 2.6 Phase 6: bridge_init

**Entry conditions:**
- Feature bits are complete
- MQTT is connected
- MQTT client adapter is initialized

**Entry action:**
- Log "Startup: Bridge init phase"

**Per-loop work / signal handling:**
- On signal_run_loop: check if bridge is not initialized AND autodiscovery
  is complete AND MQTT is connected. If all true, call initialize_mqtt_bridge().
- On signal_mqtt_connected: same check, allows immediate init if MQTT just
  connected.

**initialize_mqtt_bridge_() actions:**
1. Apply valid-ERD filter to ErdRegistry (from FeatureBitManager results)
2. Select operating mode (poll/subscribe/auto based on config and protocol)
3. Initialize the appropriate bridge:
   - Polling: mqtt_bridge_polling_init() with ERD client, mqtt_client
     adapter interface, polling interval, publish-on-change flag
   - Subscribe: mqtt_bridge_init() with ERD client, mqtt_client adapter
     interface, host address
4. Set mqtt_bridge_initialized_ = true
5. If generate_device_config: initialize HaDiscoveryManager with device info,
   registered ERDs, and mqtt_adapter pointer

**Exit:** Transition to subscription_watch

**Component state on exit:**
- mqtt_bridge_initialized_ == true
- Either subscription_bridge_initialized_ or polling_bridge_initialized_ is true
- ErdRegistry has valid ERDs set (filtering active)
- HaDiscoveryManager is in WAITING_FOR_READY state (if enabled)

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
- Exit: when drain returns 0 (queue empty), transition to RUNNING
- Fallback: if adapter not initialized, transition to RUNNING immediately

**RUNNING:**
- Entry: queue empty
- Action: call esphome_mqtt_client_adapter_drain_pending_updates() each loop
  (drains any new updates)
- Exit: on disconnect, transition to DISCONNECTED

### 3.3 ERD Value Flow in Steady State

Once the bridge is initialized, ERD values flow through this path:

1. Appliance publishes (subscription) OR bridge polls (polling mode)
2. ERD client activity event fires (read_completed or
   subscription_publication_received)
3. handle_erd_client_activity_() routes the event:
   - Subscription publications: track activity, reset HA discovery quiet window
   - Read completions: delivered to the bridge's internal handlers
4. The bridge (mqtt_bridge or mqtt_bridge_polling) calls
   mqtt_client_update_erd(&mqtt_client_adapter_.interface, erd, data, size)
5. The adapter queues the update in the pending map (deduplicated by ERD)
6. The MQTT FSM drains up to 5 pending updates per loop iteration
7. Each drain publishes to the MQTT broker via the ESPHome MQTT client

---

## 4. MQTT Connection Rules

### 4.1 Connection Detection

- Connection state is checked by reading mqtt::global_mqtt_client->is_connected()
  in the MQTT FSM at the top of loop()
- The MQTT FSM is the SINGLE authority on connection state for the component

### 4.2 Connect Edge

- When transitioning from DISCONNECTED to connected:
  - Log "MQTT connected"
  - Send signal_mqtt_connected to the startup HSM (may unblock feature_bits
    or bridge_init phases)
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
- In FLUSHING: pending updates are drained. The settle delay
  (mqtt_connected_at_ms) is NOT currently enforced in drain_pending_updates()
  (the function publishes immediately if connected).
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

### 5.1 Who Calls mqtt_client_update_erd()

**Currently:**
- mqtt_bridge (subscription mode): calls on every ERD publication received
- mqtt_bridge_polling (polling mode): calls on every ERD read completed
- FeatureBitManager: does NOT call (explicitly avoided per code comment)
- DeviceIdentityManager: does NOT call (no mqtt_client pointer)

**During startup (before bridge_init):**
- No component calls mqtt_client_update_erd()
- ERD values read during device_id and feature_bits phases are consumed
  internally by their respective managers and NOT published to MQTT

### 5.2 ErdRegistry Filtering

- The adapter checks erd_registry->is_valid(erd) before queuing an update
- Before feature_bits completes: valid_erds_ready is false, so is_valid()
  returns true for ALL ERDs (no filtering)
- After initialize_mqtt_bridge_() sets valid ERDs: only ERDs in the valid
  set pass the filter
- String-type detection (is_string_type()) is always active once string ERDs
  are populated in mqtt_client_init

### 5.3 Pending Update Queue

- The adapter uses a std::map<tiny_erd_t, PendingErdUpdate> keyed by ERD
- Repeated updates for the same ERD overwrite the previous value (dedup)
- Max 200 pending updates (safety bound)
- Drain rate: 5 per call, called once per loop iteration in FLUSHING/RUNNING
  FSM states
- On full queue: updates are dropped with a warning log

### 5.4 Write Result Publishing

- update_erd_write_result() publishes directly via global_mqtt_client
  (bypasses the pending update queue)
- If MQTT is disconnected, the write result is silently dropped
- This is inconsistent with the pending queue behavior for regular ERD updates

### 5.5 HA Discovery Publishing

- HaDiscoveryManager uses esphome_mqtt_client_adapter_publish() for async
  publishing when mqtt_adapter_ is non-null
- Falls back to mqtt_client->publish() directly when mqtt_adapter_ is null
- Discovery messages are published with retain=true

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

2. **Autodiscovery responses (in_gea3_discovery || in_gea2_discovery):**
   - Check for ERD_APPLIANCE_TYPE read_completed
   - Deliver to autodiscovery_manager_.on_broadcast_response()
   - Return immediately (do not route further)

3. **Pre-bridge reads (!mqtt_bridge_initialized_):**
   - read_completed: route to FeatureBitManager or DeviceIdentityManager
     based on should_route_to_feature_bits_()
   - read_failed: route similarly
   - These managers handle the values internally (no MQTT publish)

4. **Post-bridge reads (mqtt_bridge_initialized_):**
   - The bridge's own event subscriptions handle these (not the bridge's
     handle_erd_client_activity_() -- the bridges subscribe directly to the
     ERD client activity event)

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
2. mqtt_client_init phase -- initializes MQTT adapter, ErdRegistry string
   ERDs, FeatureBitManager
3. bridge_init phase -- applies valid ERD filter, initializes bridge,
   initializes HaDiscoveryManager

### 7.2 Destruction Order (teardown())

1. ha_discovery_manager_.cleanup() -- stops FreeRTOS task, frees resources
2. mqtt_bridge_destroy() if subscription_bridge_initialized_
3. mqtt_bridge_polling_destroy() if polling_bridge_initialized_
4. esphome_mqtt_client_adapter_destroy() if mqtt_client_adapter_initialized_
5. Component::teardown()

### 7.3 Re-initialization Rules

- The MQTT adapter is initialized exactly once (guarded by
  mqtt_client_adapter_initialized_)
- The bridge is initialized exactly once (guarded by mqtt_bridge_initialized_)
- In AUTO mode fallback: the subscription bridge is destroyed and a polling
  bridge is created in its place (check_subscription_activity_)
- Custom ERD polling can be started alongside the subscription bridge
  (start_custom_erd_polling_)
- ErdRegistry registered_erds are cleared before bridge re-initialization

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

### I3. ErdRegistry is set before the bridge reads ERDs
The ErdRegistry pointer is set on the adapter in mqtt_client_init, before
the bridge is initialized in bridge_init. The valid ERD filter is applied
in bridge_init, before the bridge starts reading ERDs.

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

### I9. FeatureBitManager does not publish during the feature_bits phase
The manager receives an i_mqtt_client_t pointer but does not call
mqtt_client_update_erd() for feature bit ERDs. This is intentional to avoid
heap pressure on ESP32-C3.

### I10. DeviceIdentityManager does not publish
The manager has no i_mqtt_client_t pointer and does not publish ERD values
to MQTT. Device ID ERDs are consumed internally only.

### I11. HaDiscoveryManager waits for a ready signal
In subscription mode: waits for HA_DISCOVERY_QUIET_MS (10s) of no new ERD
activity, with a HA_DISCOVERY_MAX_WAIT_MS (30s) safety cap.
In polling mode: waits for polling_list_complete.

### I12. The feature_bits phase gates on MQTT connection
The phase does not transition to bridge_init until BOTH feature bits are
complete AND MQTT is connected. This ensures the bridge is initialized while
MQTT is available for the subscribe/drain sequence.

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
  |
  +-- FeatureBitManager
  |     +-- i_tiny_gea3_erd_client_t (active client from autodiscovery)
  |     +-- i_mqtt_client_t (stored but not used for publishing)
  |
  +-- ErdRegistry
  |     (no runtime dependencies -- pure data container)
  |
  +-- EsphomeMqttClientAdapter
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
  |
  +-- startup_hsm
        +-- IBridgeServices* (back-pointer to GeappliancesBridge)
        +-- mqtt::global_mqtt_client (for connection checks)
```

---

## 10. Notes on Current Design Debts

These are known issues that the spec documents as current behavior. They are
NOT requirements -- they are observations of where the code stands.

### D1. HaDiscoveryManager depends on concrete ESPHome types
The run() method accepts mqtt::MQTTClientComponent*, and publish_next_entity_()
has a direct fallback to mqtt_client->publish(). This violates the abstraction
principle that MQTT operations should go through i_mqtt_client_t.

### D2. Startup HSM directly reads mqtt::global_mqtt_client
The feature_bits and bridge_init phases check MQTT connectivity by reading
mqtt::global_mqtt_client->is_connected() directly, instead of delegating to
the adapter or the MQTT FSM.

### D3. update_erd_write_result() bypasses the pending queue
Write results are published directly instead of being queued like regular ERD
updates. This means they are silently dropped on disconnect.

### D4. FeatureBitManager holds an unused i_mqtt_client_t pointer
The pointer is received in init() but never used for publishing. It exists
because the original design considered having the manager publish feature bit
ERDs, but this was abandoned due to heap pressure concerns.

### D5. handle_erd_client_activity_() does not publish ERD values during startup
ERD values read during device_id and feature_bits phases are consumed
internally but never queued for MQTT publish. This means there is a gap
between feature_bits completion and bridge_init where ERD values exist but
are not published to MQTT.
