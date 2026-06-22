# i_mqtt_client

## Purpose

Define the abstract interface through which the bridges report ERD values and receive write commands, keeping the bridge implementations independent of ESPHome.

## Interface

The `i_mqtt_client_t` struct wraps a vtable (`i_mqtt_client_api_t`) that the concrete implementation (`EsphomeMqttClientAdapter`) fills in.

### Methods

| Method | Description |
|--------|-------------|
| `mqtt_client_register_erd(self, erd)` | Register a newly discovered ERD with the MQTT adapter. The adapter creates MQTT topics for the ERD. |
| `mqtt_client_update_erd_write_result(self, erd, success, failure_reason)` | Provide the result for the most recently completed write request to an ERD. The adapter publishes the result to the appropriate MQTT topic. |
| `mqtt_client_on_write_request(self)` | Return an event pointer for subscribing to write requests received from MQTT. The bridge subscribes to this event to handle write commands. |
| `mqtt_client_on_mqtt_disconnect(self)` | Return an event pointer for subscribing to MQTT disconnect notifications. |
| `mqtt_client_on_mqtt_connect(self)` | Return an event pointer for subscribing to MQTT connect notifications. |
| `mqtt_client_publish_raw(self, topic, payload, payload_len, retain)` | Publish a raw MQTT message (C-string topic and payload). |

### Inline Wrappers

Each vtable method has a corresponding `static inline` wrapper function that dereferences the vtable and calls the implementation. This allows callers to use the interface with normal function-call syntax.

## Event Types

### Write Request

The `on_write_request` event carries `mqtt_client_on_write_request_args_t`:

| Field | Type | Description |
|-------|------|-------------|
| `erd` | `tiny_erd_t` | The target ERD |
| `data` | `const uint8_t*` | The write payload |
| `data_size` | `uint8_t` | Payload length |

## Dependencies

- `i_tiny_event.h` — event pub/sub system
- `i_tiny_gea3_erd_client.h` — GEA3 ERD client types
- `tiny_erd.h` — `tiny_erd_t` type

## Key Design Decisions

- **Abstract interface**: The bridges depend only on this interface, not on the concrete ESPHome MQTT implementation. This enables testing with mock implementations.
- **Event-driven**: Write requests and connection state changes are communicated via the `tiny_event` pub/sub system, not direct callbacks. This allows multiple subscribers (e.g., both bridges and the publisher).
- **No connection management**: The interface does not include connect/disconnect methods — connection lifecycle is managed by the adapter.

## Testing

Exercised through all bridge unit tests via the `mqtt_client_double` mock implementation.
