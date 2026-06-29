# GeappliancesBridge — Specification

## 1. Overview

### 1.1 Purpose

The main ESPHome component class that orchestrates the entire GE Appliances bridge. It manages UART interfaces for GEA2/GEA3 protocols, drives the startup state machine, handles MQTT connection lifecycle, and coordinates all sub-managers (autodiscovery, device identity, feature bits).

### 1.2 Responsibilities

- Own and construct all component instances (adapters, managers, bridges)
- Wire components together during `setup()`
- Drive the GEA2 tight-loop and delegate ongoing work in `loop()`
- Expose configuration setters called by the ESPHome code generator
- Implement `IBridgeServices` so the startup HSM can request bridge actions without depending on this concrete class

### 1.3 Not Responsible For

- Assembling the device ID (`DeviceIdentityManager`)
- Determining which ERDs are valid (`FeatureBitManager` / `ErdRegistry`)
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
| `teardown()` | Clean up bridges and MQTT adapter |

### 2.2 Configuration Setters (called from `__init__.py` code generation)

| Setter | Description |
|--------|-------------|
| `set_gea3_uart(uart)` / `set_gea2_uart(uart)` | Configure UART interfaces |
| `set_client_address(address)` | Set the bridge's bus address (default `0xE4`) |
| `set_device_id(id)` | Pre-configure a static device ID |
| `set_mode(mode)` | Set bridge mode: POLL (0), SUBSCRIBE (1), or AUTO (2) |
| `set_polling_interval(ms)` | Set polling interval (default 10000 ms) |
| `set_appliance_api_parsing(bool)` | Enable feature bit-based ERD filtering (default true) |
| `set_generate_device_config(bool)` | Enable HA discovery payload generation (default false) |
| `set_filter_config_topics(bool)` | Filter non-config topics during discovery cleanup (default true) |
| `add_custom_erd(erd)` | Add a custom ERD to poll |
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
| `handle_subscription_failed()` | Handle subscription bridge failure; trigger polling fallback in AUTO mode |
| `handle_polling_failed()` | Handle polling bridge failure; clean up or recover |
| `start_custom_erd_polling_()` | Initialize polling for user-configured custom ERDs |
| `maybe_start_custom_erd_polling_()` | Guarded entry point for custom ERD polling (prevents re-initialization) |
| `log_poll_state_transitions_()` | Debug: log polling HSM state changes |

---

## 4. Startup Sequence

The bridge progresses through a linear sequence of phases via the `startup_hsm_`:

```
protocol_stack → autodiscovery → device_id → mqtt_client_init
             → feature_bits → bridge_init → subscription_watch
             → running
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
   - **Write bridge:** Always initialized with the real host address from `autodiscovery_manager_.get_host_address()` (autodiscovery completes before bridge init).

---

## 6. GEA2 Tight Loop

When GEA2 is active, `run_protocol_stack_()` executes a 100 ms wall-clock busy loop to ensure the full TX→RX cycle at 19200 baud completes within a single `loop()` call. A manual millisecond counter (`gea2_msec_interrupt_`) drives the GEA2 interface's internal timers without starving the shared `timer_group_`.

| Constant | Value | Description |
|----------|-------|-------------|
| `GEA2_LOOP_DURATION_MS` | 100 ms | Wall-clock duration for GEA2 tight loop |
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
4. **GEA2 tight loop blocks the main loop:** During the 100 ms GEA2 tight loop, the ESPHome main loop is blocked. This is necessary for correct GEA2 half-duplex operation but limits the responsiveness of other ESPHome components during that window.
5. **Static global back-pointer:** The startup HSM uses a static global pointer (`g_bridge_instance`) to access `IBridgeServices` methods. This is safe in the single-threaded ESPHome context but would not be thread-safe in a multi-threaded environment.
---

## 11. HA Discovery Integration

The bridge integrates Home Assistant MQTT discovery via the `ha_discovery_manager_` and coordinates with the `erd_cache_publisher_` to avoid MQTT queue contention during discovery payload generation and cleanup.

### 11.1 Configuration Flags

Two boolean configuration flags control discovery behavior. Both are set via the ESPHome code generator during `setup()` and read by `loop()` to gate discovery actions.

| Flag | Default | Description |
|------|---------|-------------|
| `generate_device_config_` | `false` | When `true`, the bridge starts HA discovery once steady state is reached. Discovery runs once per boot cycle. |
| `filter_config_topics_` | `true` | When `true`, the discovery manager filters out non-config topics during cleanup (only `/config` discovery payloads are removed). Passed to `ha_discovery_manager_configure()`. |

**Configuration setters:**

```cpp
void set_generate_device_config(bool generate_device_config);
void set_filter_config_topics(bool filter_config_topics);
```

Both setters store their value directly into the corresponding member variable. They are called from `__init__.py` during ESPHome code generation based on the user's YAML configuration.

### 11.2 Discovery State Fields

| Field | Type | Description |
|-------|------|-------------|
| `ha_discovery_started_` | `bool` | Set to `true` once discovery has been initiated. Acts as a one-shot guard — discovery is started at most once per boot. Initialized to `false`. |
| `discovery_refresh_in_progress_` | `bool` | Set to `true` while a discovery cleanup refresh is running (triggered by `DiscoveryRefreshButton`). Cleared to `false` when cleanup completes and before reboot. Initialized to `false`. |
| `erd_cache_publisher_paused_` | `bool` | Tracks whether the ERD cache publisher was paused during discovery activity. Set to `true` when `pause()` is called, and `false` when `resume()` is called. Used to avoid redundant pause/resume calls across `loop()` iterations. Initialized to `false`. |
| `discovery_just_resumed_` | `bool` | Set to `true` when the publisher is resumed after discovery activity ends. Cleared to `false` once `erd_cache_mqtt_publisher_first_round_done()` returns `true`, indicating the publisher has completed a full cache round after resuming. Initialized to `false`. |

### 11.3 Discovery Lifecycle in loop()

Each `loop()` iteration performs the following discovery-related work in order:

1. **Check discovery activity:** Call `ha_discovery_manager_is_processing(&ha_discovery_manager_)` to determine if the discovery manager is in an active state (`building` or `discovering`).

2. **Pause/resume ERD cache publisher:**
   - **If discovery is active** and `erd_cache_publisher_.cache` is non-null: call `erd_cache_mqtt_publisher_pause()`. If `erd_cache_publisher_paused_` is `false`, log a debug message and set it to `true`.
   - **If discovery is not active** and `erd_cache_publisher_.cache` is non-null: call `erd_cache_mqtt_publisher_resume()`. If `erd_cache_publisher_paused_` is `true`, log a debug message, set it to `false`, and set `discovery_just_resumed_` to `true`.

3. **Check steady state after resume:** If `discovery_just_resumed_` is `true` and `erd_cache_mqtt_publisher_first_round_done()` returns `true`, log "Device is in steady state" and clear `discovery_just_resumed_`.

4. **Signal or run publisher:** If `erd_cache_publisher_.cache` is non-null and discovery is not active:
   - **On ESP-IDF:** call `erd_cache_mqtt_publisher_signal_work()` to wake the background task.
   - **On non-ESP-IDF:** call `erd_cache_mqtt_publisher_loop()` with `max_publishes=5` and `max_ms=20`.

5. **Start HA discovery (one-shot):** If `steady_state_reached_` is `true`, `ha_discovery_started_` is `false`, and `generate_device_config_` is `true`:
   - Set `ha_discovery_started_` to `true`.
   - Call `ha_discovery_manager_configure()` with device ID, model number, serial number, appliance type, `filter_config_topics_`, ERD cache pointer, and MQTT client interface.
   - Call `ha_discovery_manager_start()`.

6. **Drive discovery manager:** If `ha_discovery_manager_is_processing()` returns `true`, call `ha_discovery_manager_run()` to advance the discovery state machine.

7. **Handle cleanup completion:** If `discovery_refresh_in_progress_` is `true` (ESP-IDF only):
   - Call `ha_discovery_cleanup_run()` to advance the cleanup module.
   - When `ha_discovery_cleanup_is_done()` returns `true`: clear `discovery_refresh_in_progress_`, log "HA discovery cleanup complete, restarting device...", delay 500 ms via `vTaskDelay`, and call `esphome::App.reboot()`.

```mermaid
flowchart TD
    A[loop() entry] --> B{discovery active?}
    B -->|yes| C[pause publisher]
    B -->|no| D{publisher was paused?}
    C --> E[skip publisher work]
    D -->|yes| F[resume publisher]
    D -->|no| G[signal/run publisher]
    F --> H{first round done?}
    H -->|yes| I[log steady state]
    H -->|no| G
    I --> J{steady & !started & generate?}
    G --> J
    E --> K{manager processing?}
    J -->|yes| L[start discovery]
    J -->|no| K
    L --> K
    K -->|yes| M[run manager]
    K -->|no| N{cleanup in progress?}
    M --> N
    N -->|yes| O[run cleanup]
    N -->|no| P[continue loop]
    O --> Q{cleanup done?}
    Q -->|yes| R[reboot]
    Q -->|no| P
```

### 11.4 trigger_discovery_refresh() Flow

`trigger_discovery_refresh()` is called by `DiscoveryRefreshButton::press_action()` to initiate a discovery cleanup and device restart.

**Guard checks (in order):**

1. **Idempotency guard:** If `discovery_refresh_in_progress_` is `true`, log a warning ("Discovery refresh already in progress, ignoring") and return.
2. **Steady-state guard:** If `steady_state_reached_` is `false`, log a warning ("Cannot refresh discovery: appliance bridge not in steady state") and return.
3. **Processing guard:** If `ha_discovery_manager_is_processing()` returns `true`, log a warning ("Cannot refresh discovery: manager still processing") and return.

**On ESP-IDF, after guards pass:**

1. Log info: "Starting HA discovery cleanup..."
2. Call `ha_discovery_cleanup_configure()` with the device ID, MQTT client interface, and `esphome::millis` as the time source.
3. Call `ha_discovery_cleanup_start()`.
4. Set `discovery_refresh_in_progress_` to `true`.

On non-ESP-IDF platforms, the cleanup configure/start calls are omitted (dependency pointers are suppressed with `(void)` casts), but the guard checks still execute and `discovery_refresh_in_progress_` is set to `true`.

### 11.5 Integration with ha_discovery_manager

The bridge owns a `ha_discovery_manager_t` instance (`ha_discovery_manager_`) and drives it from `loop()`:

- **Configuration:** `ha_discovery_manager_configure()` is called once when discovery starts, passing the device identity (ID, model, serial, appliance type), the `filter_config_topics_` flag, the ERD cache, and the MQTT client interface.
- **Start:** `ha_discovery_manager_start()` transitions the manager from `IDLE` to `BUILDING` (on ESP-IDF) or directly to `COMPLETE` (on non-ESP-IDF).
- **Drive:** `ha_discovery_manager_run()` is called each `loop()` iteration while `ha_discovery_manager_is_processing()` returns `true`. This advances the manager through its state machine, decompressing JSONL chunks and publishing discovery payloads at a rate-limited interval (50 ms).
- **Completion:** When the manager reaches `COMPLETE` or `FAILED` state, `ha_discovery_manager_is_processing()` returns `false`, causing `loop()` to stop calling `run()` and resume normal ERD cache publishing.

### 11.6 Integration with ha_discovery_cleanup

The bridge accesses the embedded cleanup module via `ha_discovery_manager_.cleanup`:

- **Configuration:** `ha_discovery_cleanup_configure()` is called with the device ID, MQTT client interface, and time source. This sets up the wildcard subscription and internal buffers.
- **Start:** `ha_discovery_cleanup_start()` begins the cleanup process.
- **Drive:** `ha_discovery_cleanup_run()` is called each `loop()` iteration while `discovery_refresh_in_progress_` is `true`. This advances the cleanup through its phases (subscribe, discover, drain).
- **Completion:** `ha_discovery_cleanup_is_done()` returns `true` when all phases are complete. The bridge then clears `discovery_refresh_in_progress_`, delays 500 ms for final messages to transmit, and calls `esphome::App.reboot()`.

The cleanup module is embedded within the `ha_discovery_manager_t` struct, so the bridge does not maintain a separate cleanup instance.

### 11.7 Publisher Pause/Resume Rationale

The ERD cache publisher is paused during discovery activity to avoid competing for the ESP-IDF MQTT task's inbound/outbound queues. Without pausing, the background publisher task would continue draining cache updates while the discovery manager publishes config payloads, potentially causing:

- MQTT queue overflow, leading to dropped messages
- Retained-clear messages from cleanup being overwritten by late cache publishes
- Unpredictable ordering of discovery config vs. value messages on the broker

The pause/resume cycle ensures a clean separation: discovery messages are published without interference, then the cache publisher resumes and drains accumulated updates. The `first_round_done` mechanism confirms the publisher has completed a full cache round after resuming, providing a steady-state signal.
