# ESPHome MQTT Client Adapter

## Purpose

Adapts ESPHome's MQTT client to the `i_mqtt_client` interface expected by the tiny-gea-api bridge libraries. Handles ERD value publishing via the ERD cache MQTT publisher, write command subscriptions, and MQTT connection event notifications.

## Public API

| Function | Description |
|----------|-------------|
| `esphome_mqtt_client_adapter_init(self, device_id)` | Initialize adapter with device ID string. Wires ESPHome MQTT connect/disconnect callbacks to tiny events. |
| `esphome_mqtt_client_adapter_set_erd_registry(self, erd_registry)` | Set the ErdRegistry pointer for valid-ERD filtering, string-ERD type detection, and registered-ERD tracking. |
| `esphome_mqtt_client_adapter_subscribe_write_topic(self)` | Subscribe to the wildcard write topic `geappliances/{device_id}/erd/+/write`. No-op if MQTT client is not available. |
| `esphome_mqtt_client_adapter_drain_pending_updates(self)` | No-op: returns 0 (no pending update queue exists). |
| `esphome_mqtt_client_adapter_notify_disconnected(self)` | Publish disconnect event to bridge HSMs. |
| `esphome_mqtt_client_adapter_notify_connected(self)` | Publish connect event to bridge HSMs. |
| `esphome_mqtt_client_adapter_destroy(self)` | Unsubscribe from wildcard write topic, free heap-allocated members. |
| `esphome_mqtt_client_adapter_publish(self, topic, payload, retain)` | Publish an MQTT message via ESPHome's global MQTT client. |
| `esphome_mqtt_client_adapter_publish_raw(self, topic, payload, payload_len, retain)` | Publish raw MQTT message (C-string topic and payload). Implements the i_mqtt_client_t publish_raw vtable slot. |
| `esphome_mqtt_client_adapter_subscribe(self, topic, callback, arg)` | Subscribe to a topic with a raw C callback. Implements the i_mqtt_client_t subscribe vtable slot. |
| `esphome_mqtt_client_adapter_unsubscribe(self, topic)` | Unsubscribe from a topic. Implements the i_mqtt_client_t unsubscribe vtable slot. |
| `esphome_mqtt_client_adapter_get_pending_update_count(self)` | No-op: returns 0 (no pending update queue exists). |

## Internal i_mqtt_client API Implementation

The adapter implements the `i_mqtt_client_api_t` interface:

| Callback | Description |
|----------|-------------|
| `register_erd(erd)` | Register ERD with the ErdRegistry (if set) for valid-ERD tracking. |
| `update_erd_write_result(erd, success, reason)` | Publish write result to MQTT. |
| `on_write_request()` | Return event pointer for write request notifications. |
| `on_mqtt_disconnect()` | Return event pointer for disconnect notifications. |
| `on_mqtt_connect()` | Return event pointer for connect notifications. |
| `publish_raw(topic, payload, payload_len, retain)` | Publish raw MQTT message. |
| `subscribe(topic, callback, arg)` | Subscribe to a topic with a C callback. |
| `unsubscribe(topic)` | Unsubscribe from a topic. |

## Dependencies

- `esphome::mqtt::global_mqtt_client` — ESPHome MQTT client singleton
- `i_mqtt_client` — interface definition from tiny-gea-api
- `ErdRegistry` — valid-ERD filtering, string-ERD type detection, registered-ERD tracking
- `tiny_event` — event pub/sub system

## Key Design Decisions

- **Wildcard subscription**: Instead of subscribing to 100+ individual write topics (one per ERD), a single wildcard topic `geappliances/{device_id}/erd/+/write` is used. This eliminates:
  - 108 heap-allocated lambda closures
  - 108 IDF MQTT outbox entries (SUBSCRIBE packets)
  - A 3-second stall on MQTT reconnect from synchronous re-subscriptions
- **No pending update queue**: ERD updates are published directly to MQTT via the ERD cache MQTT publisher. There is no queue for pending updates during disconnects — `drain_pending_updates()` and `get_pending_update_count()` are no-ops returning 0.
- **No settle delay**: There is no `mqtt_connected_at_ms` timestamp or settle delay mechanism. The adapter simply fires connect/disconnect events.
- **Hex payloads**: All ERD values are published as lowercase hex strings (e.g., `%02x`). String conversion is handled at the application level, not in the MQTT adapter.
- **ErdRegistry integration**: The adapter calls `register_erd()` on the ErdRegistry (when set) to track registered ERDs. It does not call `is_valid()` or perform string-ERD type detection; those capabilities exist in ErdRegistry but are not used by this adapter.

## Testing

Covered by integration tests in `test/tests/`. The wildcard subscription and ErdRegistry integration are tested through the full bridge initialization flow.
