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
| `set_generate_device_config(bool)` | Enable HA discovery payload generation (default true) |
| `set_filter_config_topics(bool)` | Filter diagnostic entities during discovery publishing (default true) |
| `add_custom_erd(erd)` | Add a custom ERD to poll |
| `set_erd_publish_rate_sensor(sensor)` | Sensor for ERD publish rate |
| `set_erd_cache_entries_sensor(sensor)` | Sensor for ERD cache entry count |
| `set_erd_cache_updates_sensor(sensor)` | Sensor for ERD cache update count |
| `set_mqtt_publish_rate_sensor(sensor)` | Sensor for MQTT publish rate |
| `set_throttle_rate_seconds(uint8_t)` | Set ERD cache throttle rate in seconds (default 1) |

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
The bridge progresses through a linear sequence of phases via the `startup_hsm_wrapper_`:

```
startup_delay → autodiscovery → device_id → mqtt_client_init
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
| `startup_hsm_wrapper_` | `startup_hsm_wrapper_t` | Startup state machine wrapper (embeds `tiny_hsm_t` and `IBridgeServices*`) |
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
| `poll_probe_list_[POLLING_LIST_MAX_SIZE]` | `uint16_t[649]` | Pre-built probe list |

---

## 8. Invariants

1. **IBridgeServices implementation:** `GeappliancesBridge` implements `IBridgeServices`, the abstract contract consumed by the startup HSM. This eliminates `friend` declarations and lets the HSM be unit-tested with a mock.
2. **Probe list ownership:** The `poll_probe_list_` member stores the built probe list so the pointer passed to `erd_bridge_poll_init()` remains valid across the probe phase.
3. **ERD cache publisher delegation:** The `erd_cache_mqtt_publisher_` drains `update_required` entries from the shared cache and publishes them to MQTT each `loop()`, decoupling the bridges from direct MQTT interaction.
4. **Single appliance:** The bridge operates with a single discovered appliance address.
5. **Fixed capacity arrays throughout:** All data structures use fixed-capacity arrays to avoid heap allocation (custom ERDs: 64, probe list: 649, ERD cache: 200).

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
5. **Startup HSM bridge access:** The startup HSM uses the `container_of` pattern via `services_from_hsm()` to recover the `IBridgeServices` pointer from the embedded `startup_hsm_wrapper_t`. This avoids a static global back-pointer and keeps the HSM self-contained.

## 11. HA Discovery Integration

The bridge integrates Home Assistant MQTT discovery via the `ha_discovery_manager_` and coordinates with the `erd_cache_publisher_` to avoid MQTT queue contention during discovery payload generation and cleanup.

**Discovery does not run on normal boots.** HA discovery topics are retained on the MQTT broker, so the bridge skips discovery on regular reboots. Discovery runs in two scenarios:

1. **OTA reboot:** Detected in `setup()` by reading the reboot source from NVS. If the source is `"esphome.ota"`, the bridge schedules cleanup + republish + reboot after reaching steady state.
2. **Discovery Refresh button:** When pressed, the request is queued and executes once the bridge is ready (steady state, MQTT connected, device ID complete).

### 11.1 Configuration Flags

Two boolean configuration flags control discovery behavior. Both are set via the ESPHome code generator during `setup()` and read by `loop()` to gate discovery actions.

| Flag | Default | Description |
|------|---------|-------------|
| `generate_device_config_` | `true` | When `true`, the bridge runs HA discovery on OTA reboot. When `false`, the OTA path is skipped entirely. The Discovery Refresh button always works regardless of this flag (it's a user-initiated manual action). Normal boots skip discovery regardless (topics retained by MQTT broker). |
| `filter_config_topics_` | `true` | When `true`, the discovery manager filters out internal/diagnostic entities during discovery publishing. Passed to `ha_discovery_manager_configure()`. |

**Configuration setters:**

```cpp
void set_generate_device_config(bool generate_device_config);
void set_filter_config_topics(bool filter_config_topics);
```

Both setters store their value directly into the corresponding member variable. They are called from `__init__.py` during ESPHome code generation based on the user's YAML configuration.

### 11.2 Discovery State Fields

| Field | Type | Description |
|-------|------|-------------|
| `discovery_refresh_in_progress_` | `bool` | Set to `true` when `trigger_discovery_refresh()` is called. Acts as a queued request — the actual cleanup starts in `loop()` once the bridge is ready (steady state, MQTT, device ID). Cleared when cleanup begins. Initialized to `false`. |
| `erd_cache_publisher_paused_` | `bool` | Tracks whether the ERD cache publisher was paused during discovery activity. Set to `true` when `pause()` is called, and `false` when `resume()` is called. Used to avoid redundant pause/resume calls across `loop()` iterations. Initialized to `false`. |
| `discovery_just_resumed_` | `bool` | Set to `true` when the publisher is resumed after discovery activity ends. Cleared to `false` once `erd_cache_mqtt_publisher_first_round_done()` returns `true`, indicating the publisher has completed a full cache round after resuming. Initialized to `false`. |
| `ota_cleanup_needed_` | `bool` | Set to `true` in `setup()` if the reboot source is `"esphome.ota"`. Driven in `loop()` after steady state. Cleared after cleanup completes. Initialized to `false`. |
| `ota_cleanup_in_progress_` | `bool` | Set to `true` while the cleanup module is actively running (shared by OTA and Discovery Refresh paths). Cleared when cleanup completes. Initialized to `false`. |
| `ota_discovery_publishing_` | `bool` | Set to `true` while fresh discovery topics are being published after cleanup. Cleared when publishing completes. Initialized to `false`. |
| `ota_reboot_pending_` | `bool` | Set to `true` after discovery publishing completes, before the 5-second pre-reboot delay. Cleared when `safe_reboot()` is called. Initialized to `false`. |
| `ota_reboot_start_ms_` | `uint32_t` | Timestamp when the pre-reboot delay started. Initialized to `0`. |

### 11.3 OTA Reboot Detection (setup())

In `setup()`, under `#ifdef USE_ESP_IDF`, the bridge reads the reboot reason via `esp_reset_reason()`. If the reset is a software reset (`ESP_RST_SW`), it loads a stored reboot source string from NVS preferences. If the source is `"esphome.ota"`, `ota_cleanup_needed_` is set to `true`.

This ensures cleanup only runs for OTA updates, not for the bridge's own reboots (which store `"geappliances_bridge"` as the reboot source) or other software resets.

### 11.4 Discovery Lifecycle in loop()

Each `loop()` iteration performs the following discovery-related work in order:

1. **Update publisher state:** Call `update_publisher_state_()` to manage the publisher pause/resume state machine and signal or run the publisher.

2. **OTA-triggered cleanup + republish + reboot (ESP-IDF only):**
   - **Start cleanup:** If `generate_device_config_` is `true`, `ota_cleanup_needed_` is `true`, `ota_cleanup_in_progress_` is `false`, `ota_discovery_publishing_` is `false`, `ota_reboot_pending_` is `false`, and the bridge is ready (`steady_state_reached_`, `mqtt_client_adapter_initialized_`, device ID complete), configure and start the cleanup module, then set `ota_cleanup_in_progress_` to `true`.
   - **Drive cleanup:** If `ota_cleanup_in_progress_` is `true`, call `ha_discovery_cleanup_run()` each iteration. When done, clear `ota_cleanup_in_progress_` and `ota_cleanup_needed_`, destroy the cleanup module, call `ha_discovery_manager_init()` to reset the discovery manager, configure and start the discovery manager for fresh publishing, and set `ota_discovery_publishing_` to `true`.
   - **Drive discovery publishing:** If `ota_discovery_publishing_` is `true`, call `ha_discovery_manager_run()` while the manager is processing. When done, clear `ota_discovery_publishing_`, call `mark_boot_successful_for_reboot()`, and set `ota_reboot_pending_` to `true` with `ota_reboot_start_ms_` set to the current time.
   - **Wait then reboot:** If `ota_reboot_pending_` is `true`, feed the watchdog each iteration. After 5 seconds elapsed, call `esphome::App.safe_reboot()`.

3. **DiscoveryRefresh button (queued, ESP-IDF only):** If `discovery_refresh_in_progress_` is `true`, `ota_cleanup_in_progress_` is `false`, `ota_discovery_publishing_` is `false`, `ota_reboot_pending_` is `false`, and the bridge is ready, configure and start the cleanup module under `#ifdef USE_ESP_IDF`, clear `discovery_refresh_in_progress_`, and set `ota_cleanup_in_progress_` to `true`. This hands off to the same cleanup → publish → reboot path as OTA.

```mermaid
flowchart TD
    A[loop() entry] --> B[update_publisher_state_()]
    B --> C{OTA cleanup needed, generate_device_config_, & ready?}
    C -->|yes| D[start cleanup]
    C -->|no| E{OTA cleanup in progress?}
    D --> E
    E -->|yes| F[run cleanup]
    E -->|no| G{OTA discovery publishing?}
    F --> H{cleanup done?}
    H -->|yes| I[destroy cleanup, start discovery publish]
    H -->|no| G
    I --> G
    G -->|yes| J[run discovery manager]
    G -->|no| K{OTA reboot pending?}
    J --> L{discovery done?}
    L -->|yes| M[mark boot successful, set reboot pending]
    L -->|no| K
    M --> K
    K -->|yes| N{5s elapsed?}
    K -->|no| O{DiscoveryRefresh queued & ready?}
    N -->|yes| P[safe_reboot()]
    N -->|no| O
    P --> Q[device restarts]
    O -->|yes| R[start cleanup, hand off to OTA path]
    O -->|no| S[continue loop]
    R --> S
```

### 11.5 trigger_discovery_refresh() Flow

`trigger_discovery_refresh()` is called by `DiscoveryRefreshButton::press_action()` to queue a discovery cleanup and device restart.

**Behavior:**

1. **Idempotency guard:** If `discovery_refresh_in_progress_` is `true`, log a warning ("Discovery refresh already in progress, ignoring") and return.
2. **Queue the request:** Set `discovery_refresh_in_progress_` to `true` and log "Discovery refresh queued, will execute when appliance is ready".

No guard checks for steady state, MQTT readiness, or device ID are performed at press time. The request is queued and will execute in `loop()` once all prerequisites are met. This allows the user to press the button at any time, even during startup.

### 11.6 mark_boot_successful_for_reboot()

Before rebooting after cleanup + republish, `mark_boot_successful_for_reboot()` is called to:

1. **Clear the safe mode boot loop counter:** Writes `0` to the ESPHome preferences key matching `SAFE_MODE_RTC_KEY` (233825507UL) and syncs immediately. This prevents the device from entering safe mode due to rapid reboots during the cleanup → publish → reboot cycle.
2. **Cancel OTA rollback:** Calls `esp_ota_mark_app_valid_cancel_rollback()` to mark the current OTA partition as valid, preventing the bootloader from rolling back to the previous partition on the next boot.

This is critical because the cleanup → publish → reboot cycle involves two rapid reboots in succession, which would otherwise trigger ESPHome's safe mode protection.

### 11.7 Integration with ha_discovery_manager

The bridge owns a `ha_discovery_manager_t` instance (`ha_discovery_manager_`) and drives it from `loop()`:

- **Configuration:** `ha_discovery_manager_configure()` is called when discovery starts (during OTA or Discovery Refresh flow), passing the device identity (ID, model, serial, appliance type), the `filter_config_topics_` flag, the ERD cache, and the MQTT client interface.
- **Start:** `ha_discovery_manager_start()` transitions the manager from `IDLE` to `BUILDING`.
- **Drive:** `ha_discovery_manager_run()` is called each `loop()` iteration while `ha_discovery_manager_is_processing()` returns `true`. This advances the manager through its state machine, decompressing JSONL chunks and publishing discovery payloads at a rate-limited interval (50 ms).
- **Completion:** When the manager reaches `COMPLETE` or `FAILED` state, `ha_discovery_manager_is_processing()` returns `false`, signaling that discovery publishing is done.

### 11.8 Integration with ha_discovery_cleanup

The bridge accesses the embedded cleanup module via `ha_discovery_manager_.cleanup`. Both OTA and Discovery Refresh paths use the same cleanup module and share the `ota_cleanup_in_progress_` flag:

- **Configuration:** `ha_discovery_cleanup_configure()` is called with the device ID, MQTT client interface, and time source.
- **Start:** `ha_discovery_cleanup_start()` begins the cleanup process.
- **Drive:** `ha_discovery_cleanup_run()` is called each `loop()` iteration while `ota_cleanup_in_progress_` is `true`.
- **Completion:** `ha_discovery_cleanup_is_done()` returns `true` when all phases are complete. The bridge then clears `ota_cleanup_in_progress_`, destroys the cleanup module, and proceeds to publish fresh discovery topics.

### 11.9 Reboot with safe_reboot()

After cleanup and discovery publishing complete, the bridge reboots using `esphome::App.safe_reboot()` instead of `esphome::App.reboot()`. `safe_reboot()` performs a graceful MQTT disconnect before resetting, ensuring a clean session end on the broker. This prevents the broker from thinking the device is still connected, which could cause stale availability topics or delayed reconnection events.

A 5-second delay precedes the reboot to allow final discovery messages to transmit and the heap to stabilize. The reboot also defragments the heap after the memory-intensive cleanup and publish cycle.

### 11.10 Publisher Pause/Resume Rationale

The ERD cache publisher is paused during discovery activity to avoid competing for the ESP-IDF MQTT task's inbound/outbound queues. Without pausing, the background publisher task would continue draining cache updates while the discovery manager publishes config payloads, potentially causing:

- MQTT queue overflow, leading to dropped messages
- Retained-clear messages from cleanup being overwritten by late cache publishes
- Unpredictable ordering of discovery config vs. value messages on the broker

The pause/resume cycle ensures a clean separation: discovery messages are published without interference, then the cache publisher resumes and drains accumulated updates. The `first_round_done` mechanism confirms the publisher has completed a full cache round after resuming, clearing `discovery_just_resumed_`. The `steady_state_reached_` flag is set separately by `check_steady_state()` (called from the startup HSM), which checks bridge states directly; this is the signal that gates HA discovery startup.
