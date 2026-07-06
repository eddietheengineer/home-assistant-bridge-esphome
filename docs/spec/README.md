# Specifications

Detailed behavioral contracts for each module. Each spec documents purpose, interface, behavior, and notes.

## By Category

### Startup

| Spec | Module |
|---|---|
| [geappliances_bridge.md](./geappliances_bridge.md) | Main ESPHome component entry point |
| [geappliances_bridge_startup_hsm.md](./geappliances_bridge_startup_hsm.md) | 8-phase startup state machine |
| [autodiscovery_manager.md](./autodiscovery_manager.md) | GEA bus broadcast discovery |
| [device_identity_manager.md](./device_identity_manager.md) | Appliance identity ERD reading |
| [feature_bit_manager.md](./feature_bit_manager.md) | Feature bit ERD parsing |
| [i_bridge_services.md](./i_bridge_services.md) | HSM-to-bridge abstract interface |

### Data Bridges

| Spec | Module |
|---|---|
| [erd_bridge_poll.md](./erd_bridge_poll.md) | Periodic ERD polling |
| [erd_bridge_subscribe.md](./erd_bridge_subscribe.md) | Real-time ERD subscription |
| [erd_write_bridge.md](./erd_write_bridge.md) | MQTT-to-appliance write relay |
| [erd_poll_list_builder.md](./erd_poll_list_builder.md) | Poll list construction |
| [polling_bridge_behavior.md](./polling_bridge_behavior.md) | Polling bridge behavioral requirements |

### Data Layer

| Spec | Module |
|---|---|
| [erd_cache.md](./erd_cache.md) | Fixed-size ERD value cache |
| [erd_registry.md](./erd_registry.md) | Valid/registered ERD sets |
| [erd_cache_mqtt_publisher.md](./erd_cache_mqtt_publisher.md) | Cache-to-MQTT publishing |

### Transport Adapters

| Spec | Module |
|---|---|
| [esphome_mqtt_client_adapter.md](./esphome_mqtt_client_adapter.md) | ESPHome MQTT client bridge |
| [esphome_uart_adapter.md](./esphome_uart_adapter.md) | ESPHome UART bridge |
| [gea2_erd_client_adapter.md](./gea2_erd_client_adapter.md) | GEA2-to-GEA3 client adapter |
| [i_mqtt_client.md](./i_mqtt_client.md) | Abstract MQTT client interface |

### Home Assistant Discovery

| Spec | Module |
|---|---|
| [ha_discovery_manager.md](./ha_discovery_manager.md) | HA MQTT discovery payload generation |
| [ha_discovery_cleanup.md](./ha_discovery_cleanup.md) | Stale discovery topic removal |
| [discovery_refresh_button.md](./discovery_refresh_button.md) | User-triggered discovery refresh |
| [mqtt_data_publishing.md](./mqtt_data_publishing.md) | MQTT data publishing requirements |