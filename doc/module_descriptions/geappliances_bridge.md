# GeappliancesBridge

## Purpose

The main ESPHome component class that orchestrates the entire GE Appliances bridge. It manages UART interfaces for GEA2/GEA3 protocols, drives the startup state machine, handles MQTT connection lifecycle, and coordinates all sub-managers (autodiscovery, device identity, feature bits, HA discovery).

## Public API

| Method | Description |
|--------|-------------|
| `setup()` | Initialize timer group, UART adapters, ERD clients, GEA interfaces, managers |
| `loop()` | Drive protocol stack and startup HSM |
| `dump_config()` | Log current configuration and state |
| `get_setup_priority()` | Returns `setup_priority::DATA` (600) — after UART, before MQTT |
| `teardown()` | Clean up HA discovery, bridges, and MQTT adapter |

### Configuration Setters (called from `__init__.py` code generation)

| Setter | Description |
|--------|-------------|
| `set_gea3_uart(uart)` / `set_gea2_uart(uart)` | Configure UART interfaces |
| `set_client_address(address)` | Set the bridge's bus address (default 0xE4) |
| `set_device_id(id)` | Pre-configure a static device ID |
| `set_mode(mode)` | Set bridge mode: POLL (0), SUBSCRIBE (1), or AUTO (2) |
| `set_polling_interval(ms)` | Set polling interval (default 10000 ms) |
| `set_polling_only_publish_on_change(bool)` | Only publish ERD values when they change |
| `set_appliance_api_parsing(bool)` | Enable feature bit-based ERD filtering (default true) |
| `set_generate_device_config(bool)` | Enable device config generation |
| `add_custom_erd(erd)` | Add a custom ERD to poll |
| `set_ha_discovery_base_url(url)` | Override the HA discovery JSONL base URL |

## Protected Methods

| Method | Description |
|--------|-------------|
| `on_mqtt_connected_()` | Handle MQTT (re)connection — initialize adapter, flush pending updates |
| `notify_mqtt_disconnected_()` | Notify adapter of disconnect |
| `handle_erd_client_activity_(args)` | Route ERD activity to appropriate manager (autodiscovery, device ID, feature bits) |
| `initialize_mqtt_client_()` | Create and configure the MQTT client adapter |
| `initialize_mqtt_bridge_()` | Initialize subscription or polling bridge based on mode |
| `run_protocol_stack_()` | Drive GEA2/GEA3 hardware (includes GEA2 tight loop) |
| `start_feature_bit_reading_()` | Start the feature bit read sequence |
| `check_subscription_activity_()` | Check if subscription mode is receiving data (AUTO mode fallback) |
| `start_custom_erd_polling_()` | Initialize polling for user-configured custom ERDs |
| `maybe_start_custom_erd_polling_()` | Guarded entry point for custom ERD polling (prevents re-initialization) |
| `configure_polling_optional_lists_()` | Set up optional ERD lists for the polling bridge (appliance API filter, custom ERDs) |
| `log_poll_state_transitions_()` | Debug: log polling HSM state changes |
| `on_ha_discovery_erd_seen_(erd)` | Callback invoked when HA discovery publishes an ERD |

## Startup Sequence

The bridge progresses through a linear sequence of phases via the `startup_hsm_`:

```
protocol_stack → autodiscovery → device_id → mqtt_client_init
             → feature_bits → bridge_init → subscription_watch
             → ha_discovery → running
```

## Dependencies

- ESPHome `Component` base class
- ESPHome `uart::UARTComponent` — UART interfaces
- ESPHome `mqtt::MQTTClientComponent` — MQTT client
- `tiny_gea3_interface`, `tiny_gea3_erd_client` — GEA3 protocol stack
- `tiny_gea2_interface`, `tiny_gea2_erd_client` — GEA2 protocol stack
- All sub-managers: `AutodiscoveryManager`, `DeviceIdentityManager`, `FeatureBitManager`, `HaDiscoveryManager`
- Adapters: `esphome_uart_adapter`, `esphome_mqtt_client_adapter`, `gea2_erd_client_adapter`
- Bridges: `mqtt_bridge`, `mqtt_bridge_polling`
- `tiny_hsm`, `tiny_timer` — state machine and timer infrastructure

## Key Design Decisions

- **GEA2 tight loop**: When GEA2 is active, a 200 ms wall-clock busy loop ensures the full TX→RX cycle at 19200 baud completes within a single `loop()` call. A manual millisecond counter drives the GEA2 interface's internal timers without starving the shared timer group.
- **Bridge modes**: Three modes — POLL (always poll), SUBSCRIBE (always subscribe), AUTO (try subscribe, fall back to polling after 30 s if no activity).
- **Legacy member sync**: During the god class refactoring, legacy member variables are retained and synced from the extracted managers for backward compatibility. These will be removed in a future cleanup.
- **Phase timeouts**: Device ID phase has a 30 s timeout, feature bits phase has a 60 s timeout — both prevent the startup HSM from stalling indefinitely.

## Testing

Covered by integration tests in `test/tests/` that exercise the full startup sequence, mode switching, and error recovery paths.
