# GeappliancesBridge — Specification

## 1. Overview

### 1.1 Purpose

The main ESPHome component class that orchestrates the entire GE Appliances bridge. It manages UART interfaces for GEA2/GEA3 protocols, drives the startup state machine, handles MQTT connection lifecycle, and coordinates all sub-managers (autodiscovery, device identity, feature bits, HA discovery).

### 1.2 Responsibilities

- Own and construct all component instances (adapters, managers, bridges)
- Wire components together during `setup()`
- Drive the GEA2 tight-loop and delegate ongoing work in `loop()`
- Expose configuration setters called by the ESPHome code generator
- Implement `IBridgeServices` so the startup HSM can request bridge actions without depending on this concrete class

### 1.3 Not Responsible For

- Assembling the device ID (`DeviceIdentityManager`)
- Determining which ERDs are valid (`FeatureBitManager` / `ErdRegistry`)
- Publishing HA discovery payloads (`HaDiscoveryManager`)
- MQTT connection lifecycle (`EsphomeMqttClientAdapter`)
- Startup phase sequencing (`StartupHsm`)

---

## 2. Public API

### 2.1 ESPHome Component Lifecycle

| Method | Description |
|--------|-------------|
| `setup()` | Initialize timer group, UART adapters, ERD clients, GEA interfaces, managers |
| `loop()` | Drive protocol stack and startup HSM |
| `dump_config()` | Log current configuration and state |
| `get_setup_priority()` | Returns `setup_priority::DATA` (600) — after MQTT (50), same as UART |
| `teardown()` | Clean up HA discovery, bridges, and MQTT adapter |

### 2.2 Configuration Setters (called from `__init__.py` code generation)

| Setter | Description |
|--------|-------------|
| `set_gea3_uart(uart)` / `set_gea2_uart(uart)` | Configure UART interfaces |
| `set_client_address(address)` | Set the bridge's bus address (default `0xE4`) |
| `set_device_id(id)` | Pre-configure a static device ID |
| `set_mode(mode)` | Set bridge mode: POLL (0), SUBSCRIBE (1), or AUTO (2) |
| `set_polling_interval(ms)` | Set polling interval (default 10000 ms) |
| `set_polling_only_publish_on_change(bool)` | Only publish ERD values when they change |
| `set_appliance_api_parsing(bool)` | Enable feature bit-based ERD filtering (default true) |
| `set_generate_device_config(bool)` | Enable device config generation |
| `add_custom_erd(erd)` | Add a custom ERD to poll |
| `set_ha_discovery_base_url(url)` | Override the HA discovery JSONL base URL |
| `set_erd_publish_rate_sensor(sensor)` | Sensor for ERD publish rate |
| `set_erd_cache_entries_sensor(sensor)` | Sensor for ERD cache entry count |
| `set_erd_cache_updates_sensor(sensor)` | Sensor for ERD cache update count |
| `set_mqtt_publish_rate_sensor(sensor)` | Sensor for MQTT publish rate |

---

## 3. Protected Methods

| Method | Description |
|--------|-------------|
| `handle_erd_client_activity_(args)` | Route ERD activity to appropriate manager (autodiscovery, device ID, feature bits) |
| `should_route_to_feature_bits_(erd)` | Decide whether an ERD read goes to FeatureBitManager or DeviceIdentityManager |
| `initialize_mqtt_client_()` | Create and configure the MQTT client adapter |
| `initialize_erd_bridge_()` | Initialize subscription or polling bridge based on mode |
| `run_protocol_stack_()` | Drive GEA2/GEA3 hardware (includes GEA2 tight loop) |
| `start_feature_bit_reading_()` | Start the feature bit read sequence |
| `check_subscription_activity_()` | Check if subscription mode is receiving data (AUTO mode fallback) |
| `start_custom_erd_polling_()` | Initialize polling for user-configured custom ERDs |
| `maybe_start_custom_erd_polling_()` | Guarded entry point for custom ERD polling (prevents re-initialization) |
| `log_poll_state_transitions_()` | Debug: log polling HSM state changes |
| `on_ha_discovery_erd_seen_(erd)` | Callback invoked when HA discovery publishes an ERD |
| `on_poll_discovery_complete_()` | Callback from polling bridge when probe phase completes |

---

## 4. Startup Sequence

The bridge progresses through a linear sequence of phases via the `startup_hsm_`:

```
protocol_stack → autodiscovery → device_id → mqtt_client_init
             → feature_bits → bridge_init → subscription_watch
             → ha_discovery → running
```

Each phase is driven by the startup HSM, which invokes `IBridgeServices` methods on the bridge to perform work and check completion.

---

## 5. Bridge Initialization Flow

During `startup_state_bridge_init`, `initialize_erd_bridge_()` runs:

1. **Apply ERD filter:** If appliance API parsing is enabled and complete, sets the valid-ERD filter on the registry.
2. **Select mode:** Determines polling vs. subscription based on mode setting and GEA2/GEA3 protocol.
3. **Build probe list:** Calls `build_poll_list_()` (which delegates to `erd_poll_list_builder`) to build the list of ERDs to probe.
4. **Initialize bridges:**
   - **Polling mode:** Initializes `erd_bridge_poll_` with the probe list, known host address, and appliance type.
   - **Subscription mode:** Initializes `erd_bridge_subscribe_` with the known host address.
   - **Write bridge:** Always initialized with the broadcast address (updated after appliance identification).
5. **Defer HA discovery:** If enabled, initializes `ha_discovery_manager_` — it starts when the bridge signals readiness.

---

## 6. GEA2 Tight Loop

When GEA2 is active, `run_protocol_stack_()` executes a 200 ms wall-clock busy loop to ensure the full TX→RX cycle at 19200 baud completes within a single `loop()` call. A manual millisecond counter (`gea2_msec_interrupt_`) drives the GEA2 interface's internal timers without starving the shared `timer_group_`.

| Constant | Value | Description |
|----------|-------|-------------|
| `GEA2_LOOP_DURATION_MS` | 200 ms | Wall-clock duration for GEA2 tight loop |
| `GEA3_LOOP_DURATION_MS` | 10 ms | Wall-clock duration for GEA3 protocol tick |

---

## 7. Data Structures

Key member variables:

| Member | Type | Description |
|--------|------|-------------|
| `startup_hsm_` | `tiny_hsm_t` | Startup state machine |
| `uart_` / `gea2_uart_` | `UARTComponent*` | UART interfaces |
| `erd_cache_` | `erd_cache_t` | Shared ERD cache |
| `erd_cache_publisher_` | `erd_cache_mqtt_publisher_t` | MQTT publisher |
| `erd_registry_` | `ErdRegistry` | Valid/registered ERD tracking |
| `mqtt_client_adapter_` | `esphome_mqtt_client_adapter_t` | MQTT adapter |
| `uart_adapter_` / `gea2_uart_adapter_` | `esphome_uart_adapter_t` | UART adapters |
| `erd_client_` / `gea2_erd_client_` | ERD client | GEA3/GEA2 ERD clients |
| `gea2_erd_client_adapter_` | `gea2_erd_client_adapter_t` | GEA2→GEA3 adapter |
| `erd_bridge_subscribe_` | `erd_bridge_subscribe_t` | Subscription bridge |
| `erd_bridge_poll_` | `erd_bridge_poll_t` | Polling bridge |
| `erd_write_bridge_` | `erd_write_bridge_t` | Write bridge |
| `autodiscovery_manager_` | `AutodiscoveryManager` | Appliance discovery |
| `device_identity_manager_` | `DeviceIdentityManager` | Device ID generation |
| `feature_bit_manager_` | `FeatureBitManager` | Feature bit reading |
| `ha_discovery_manager_` | `HaDiscoveryManager` | HA discovery |
| `timer_group_` | `tiny_timer_group_t` | Shared timer group |
| `custom_erds_[CUSTOM_ERDS_MAX]` | `tiny_erd_t[64]` | User-configured custom ERDs |
| `poll_probe_list_[POLLING_LIST_MAX_SIZE]` | `uint16_t[645]` | Pre-built probe list |

---

## 8. Invariants

1. **IBridgeServices implementation:** `GeappliancesBridge` implements `IBridgeServices`, the abstract contract consumed by the startup HSM. This eliminates `friend` declarations and lets the HSM be unit-tested with a mock.
2. **Probe list ownership:** The `poll_probe_list_` member stores the built probe list so the pointer passed to `erd_bridge_poll_init()` remains valid across the probe phase.
3. **ERD cache publisher delegation:** The `erd_cache_mqtt_publisher_` drains `update_required` entries from the shared cache and publishes them to MQTT each `loop()`, decoupling the bridges from direct MQTT interaction.
4. **Single appliance:** The bridge operates with a single discovered appliance address.
5. **Fixed capacity arrays throughout:** All data structures use fixed-capacity arrays to avoid heap allocation (custom ERDs: 64, probe list: 645, ERD cache: 200).
6. **Phase timeouts:** Device ID phase has a 30 s timeout, feature bits phase has a 60 s timeout — both prevent the startup HSM from stalling indefinitely.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| ESPHome `Component` base class | Lifecycle (setup, loop, teardown) |
| ESPHome `uart::UARTComponent` | UART interfaces |
| ESPHome `mqtt::MQTTClientComponent` | MQTT client |
| `tiny_gea3_interface`, `tiny_gea3_erd_client` | GEA3 protocol stack |
| `tiny_gea2_interface`, `tiny_gea2_erd_client` | GEA2 protocol stack |
| `AutodiscoveryManager` | Appliance discovery |
| `DeviceIdentityManager` | Device ID generation |
| `FeatureBitManager` | Feature bit reading |
| `HaDiscoveryManager` | HA discovery |
| `esphome_uart_adapter` | UART → i_tiny_uart |
| `esphome_mqtt_client_adapter` | ESPHome MQTT → i_mqtt_client |
| `gea2_erd_client_adapter` | GEA2 → GEA3 interface |
| `erd_bridge_subscribe`, `erd_bridge_poll`, `erd_write_bridge` | ERD bridges |
| `erd_poll_list_builder` | Probe list construction |
| `erd_registry` | Valid/registered ERD tracking |
| `erd_cache_mqtt_publisher` | Cache → MQTT publishing |
| `erd_bridge_common.h` | Shared signals, timing, utilities |
| `tiny_hsm`, `tiny_timer` | State machine and timer infrastructure |

---

## 10. Known Limitations

1. **Single appliance:** The bridge operates with a single discovered appliance address. Multi-appliance support would require significant architectural changes.
2. **Fixed capacity arrays:** All data structures use fixed-capacity arrays. If the appliance supports more ERDs than the cache can hold (200), updates are silently dropped.
3. **No rollback:** The startup sequence is linear — once a phase completes, it does not re-run. If a phase fails, the bridge continues with fallback values.
4. **GEA2 tight loop blocks the main loop:** During the 200 ms GEA2 tight loop, the ESPHome main loop is blocked. This is necessary for correct GEA2 half-duplex operation but limits the responsiveness of other ESPHome components during that window.
5. **Static global back-pointer:** The startup HSM uses a static global pointer (`g_bridge_instance`) to access `IBridgeServices` methods. This is safe in the single-threaded ESPHome context but would not be thread-safe in a multi-threaded environment.
