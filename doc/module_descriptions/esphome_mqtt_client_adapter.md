# ESPHome MQTT Client Adapter

## Purpose

Adapts ESPHome's MQTT client to the `i_mqtt_client` interface expected by the tiny-gea-api bridge libraries. Handles ERD value publishing, write command subscriptions, pending update queuing during MQTT disconnects, and ERD filtering for appliance API parsing mode.

## Public API

| Function | Description |
|----------|-------------|
| `esphome_mqtt_client_adapter_init(self, device_id)` | Initialize adapter with device ID string |
| `esphome_mqtt_client_adapter_set_valid_erds_filter(self, filter)` | Set optional ERD filter for appliance API parsing mode |
| `esphome_mqtt_client_adapter_set_registered_erds_out(self, out)` | Set output set to track which ERDs are registered |
| `esphome_mqtt_client_adapter_notify_disconnected(self)` | Reset connect timestamp, publish disconnect event to bridge HSMs |
| `esphome_mqtt_client_adapter_notify_connected(self)` | Subscribe wildcard write topic (once), flush pending updates |
| `esphome_mqtt_client_adapter_destroy(self)` | Free heap-allocated members |

## Internal i_mqtt_client API Implementation

The adapter implements the `i_mqtt_client_api_t` interface:

| Callback | Description |
|----------|-------------|
| `register_erd(erd)` | Track ERD in `registered_erds_out` set |
| `update_erd(erd, value, size)` | Publish ERD value to MQTT or queue if disconnected |
| `update_erd_write_result(erd, success, reason)` | Publish write result to MQTT |
| `on_write_request()` | Return event pointer for write request notifications |
| `on_mqtt_disconnect()` | Return event pointer for disconnect notifications |

## Dependencies

- `esphome::mqtt::global_mqtt_client` — ESPHome MQTT client singleton
- `i_mqtt_client` — interface definition from tiny-gea-api
- `tiny_event` — event pub/sub system

## Key Design Decisions

- **Wildcard subscription**: Instead of subscribing to 100+ individual write topics (one per ERD), a single wildcard topic `geappliances/{device_id}/erd/+/write` is used. This eliminates:
  - 108 heap-allocated lambda closures
  - 108 IDF MQTT outbox entries (SUBSCRIBE packets)
  - A 3-second stall on MQTT reconnect from synchronous re-subscriptions
- **Pending update queue**: When MQTT is disconnected, ERD updates are queued (keyed by ERD, so repeated updates overwrite rather than append). Max 200 pending updates, flushed at 5 per `notify_connected()` call to avoid stalling the main loop.
- **Settle delay**: After reconnect, pending updates are not flushed immediately — the `mqtt_connected_at_ms` timestamp gates the flush to give the IDF MQTT task time to process the broker's reconnect backlog.
- **Hex payloads**: All ERD values are published as uppercase hex strings. String conversion is handled at the application level, not in the MQTT adapter.

## Testing

Covered by integration tests in `test/tests/`. The pending update queue, wildcard subscription, and ERD filtering are tested through the full bridge initialization flow.
