# esphome_mqtt_client_adapter — Specification

## 1. Overview

### 1.1 Purpose

Implement the `i_mqtt_client_t` interface for the ESPHome bridge, providing the MQTT transport layer through which bridge modules (`erd_bridge_subscribe`, `erd_bridge_poll`, `erd_write_bridge`) publish ERD values and receive write commands. The adapter bridges ESPHome's global MQTT client singleton to the abstract `i_mqtt_client` API expected by the tiny-gea-api bridge libraries.

### 1.2 Responsibilities

- Implement the `i_mqtt_client_t` vtable interface (`i_mqtt_client_api_t`)
- Publish ERD value updates to `geappliances/{device_id}/erd/0x{ERD}/value` topics via ESPHome's MQTT client
- Publish write results to `geappliances/{device_id}/erd/0x{ERD}/write_result` topics
- Provide MQTT connect/disconnect events (`tiny_event_t`) for publisher coordination
- Accept write commands through a single wildcard subscription topic rather than per-ERD subscriptions
- Queue ERD updates during MQTT disconnect and flush on reconnect with a settle delay *(aspirational; not yet implemented)*
- Delegate ERD registration tracking and valid-ERD filtering to `ErdRegistry`
- Provide a `publish()` helper for arbitrary MQTT message publishing
- Support topic subscription and unsubscription via `subscribe()`/`unsubscribe()` vtable slots

### 1.3 Not Responsible For

- Deciding which ERDs to publish (filtering is applied via `ErdRegistry`)
- Managing bridge lifecycle or startup phases
- ERD value serialization or string conversion
- MQTT connection management (connect, disconnect, reconnection — handled by ESPHome)

---

## 2. Data Structures

### 2.1 Adapter struct

```c
typedef struct {
  i_mqtt_client_t interface;
  const char* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  tiny_event_t on_mqtt_connect_event;
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
  uint8_t write_payload_buffer_[32];
  uint8_t write_payload_size_;
  char write_topic_[128];
} esphome_mqtt_client_adapter_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `interface` | `i_mqtt_client_t` | Handle wrapping the vtable pointer; set during `init()` |
| `device_id` | `const char*` | Raw pointer to caller-owned string; used in topic construction. Not heap-allocated by the adapter. |
| `on_write_request_event` | `tiny_event_t` | Event published when a write command arrives via MQTT |
| `on_mqtt_disconnect_event` | `tiny_event_t` | Event published when the MQTT client disconnects |
| `on_mqtt_connect_event` | `tiny_event_t` | Event published when the MQTT client connects |
| `erd_registry` | `ErdRegistry*` | Optional pointer; when non-null, provides valid-ERD filtering, string-ERD type detection, and registered-ERD tracking |
| `write_payload_buffer_` | `uint8_t[32]` | Buffer for decoded hex payload from write topic (max 32 bytes / 64 hex chars). Currently unused at member level; decoding uses a local stack buffer inside the subscribe lambda. |
| `write_payload_size_` | `uint8_t` | Number of decoded bytes in `write_payload_buffer_` |
| `write_topic_` | `char[128]` | Tracked write topic string, populated by `subscribe_write_topic()`. Used for unsubscribe during `destroy()`. |

### 2.2 Static vtable

A single `static const i_mqtt_client_api_t api` instance is defined at file scope and shared across all adapter instances. The vtable is assigned to `self->interface.api` during `init()` and never modified at runtime.

---

## 3. Public API

### 3.1 `esphome_mqtt_client_adapter_init`

```c
void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);
```
Initialize the adapter. Stores the raw `device_id` pointer directly (no copy or heap allocation). The caller is responsible for ensuring the pointed-to string outlives the adapter (guaranteed in ESPHome since YAML config strings are static). Initializes the three `tiny_event_t` instances. Wires ESPHome MQTT client connect/disconnect callbacks to the adapter's notify functions. If the MQTT client is already connected at the time of init, fires the connect event immediately so downstream publishers see the correct state on their first `loop()`.

**Parameters:**
- `self` — Pointer to uninitialized adapter struct (stack or static allocation; caller-owned)
- `device_id` — Null-terminated device ID string (pointer stored directly; caller must ensure the string outlives the adapter)

### 3.2 `esphome_mqtt_client_adapter_set_erd_registry`

```c
void esphome_mqtt_client_adapter_set_erd_registry(
  esphome_mqtt_client_adapter_t* self,
  esphome::geappliances_bridge::ErdRegistry* erd_registry);
```

Set the optional `ErdRegistry` pointer. When non-null, `register_erd()` delegates to the registry for tracking. Must be called after `init()` and before any ERD registration occurs.

### 3.3 `esphome_mqtt_client_adapter_notify_disconnected`

```c
void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self);
```

Publish the `on_mqtt_disconnect_event` to notify subscribers that MQTT is down. Called automatically by the ESPHome MQTT client's disconnect callback, or manually during test setups.

### 3.4 `esphome_mqtt_client_adapter_notify_connected`

```c
void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self);
```
Publish the `on_mqtt_connect_event` to notify subscribers that MQTT is up. Called automatically by the ESPHome MQTT client's connect callback.

**Note:** In a future implementation, this may also trigger flushing the pending update queue after a settle delay. Currently it only publishes the event.

### 3.5 `esphome_mqtt_client_adapter_subscribe_write_topic`

```c
void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self);
Subscribe to the wildcard write topic `geappliances/{device_id}/erd/+/write`. The constructed topic is saved in `write_topic_` for later unsubscribe. When a message arrives, the adapter parses the ERD ID from the topic path (e.g., `0x7701` from `.../erd/0x7701/write`), decodes the hex-encoded payload into a **local stack buffer** `local_buffer[32]` inside the lambda (not `write_payload_buffer_`), and publishes `on_write_request_event` with the decoded data. The local buffer is safe because `tiny_event_publish()` is synchronous — the subscriber callback runs to completion before the lambda returns, so the stack buffer remains valid for the duration of event delivery. The `erd_write_bridge` receives the event and dispatches the write to the ERD client.

**Note:** The header file doc comment for this function incorrectly states it is a "No-op". The implementation is fully functional and actively subscribes to the wildcard write topic.

### 3.6 `esphome_mqtt_client_adapter_drain_pending_updates`

```c
size_t esphome_mqtt_client_adapter_drain_pending_updates(
  esphome_mqtt_client_adapter_t* self);
```

Currently a no-op returning 0. In a future implementation, this would flush pending ERD updates accumulated during a disconnect, returning the number of updates drained. Updates would be flushed in batches (e.g., 5 per call) to avoid stalling the main loop.

### 3.7 `esphome_mqtt_client_adapter_destroy`

```c
void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);
```

Unregister all MQTT callbacks and clean up adapter state. If the global MQTT client is non-null, sets `on_connect` and `on_disconnect` callbacks to `nullptr`, and unsubscribes from the write topic (if `write_topic_` was populated). Nulls `device_id` and clears `write_topic_` (sets first byte to `'\0'`). Does not `delete` `device_id` since it is a raw pointer to caller-owned memory, not a heap allocation. Safe to call multiple times.

### 3.8 `esphome_mqtt_client_adapter_get_pending_update_count`

```c
size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self);
```

Return the number of pending updates queued during a disconnect. In the current implementation this always returns 0.

### 3.9 `esphome_mqtt_client_adapter_publish`

```c
void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* self,
  const std::string& topic,
  const std::string& payload,
  bool retain);
```

Publish an MQTT message using `std::string` arguments. Guards against null or disconnected MQTT client.

### 3.10 `esphome_mqtt_client_adapter_publish_raw`

```c
void esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);
```

Publish a raw MQTT message with C-string topic and payload. Implements the `publish_raw` vtable slot of `i_mqtt_client_api_t`. Guards against null or disconnected MQTT client.

---

## 4. i_mqtt_client API Implementation

The adapter implements all eight vtable slots of `i_mqtt_client_api_t`:

### 4.1 `register_erd`

```c
void register_erd(i_mqtt_client_t* self, tiny_erd_t erd);
```

Track the ERD as registered. When `erd_registry` is non-null, delegates to `ErdRegistry::register_erd()`. Logs the ERD at debug level. Called by the polling and subscription bridges when they discover an ERD to expose.

### 4.2 `update_erd_write_result`

```c
void update_erd_write_result(
  i_mqtt_client_t* self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason);
```

Publish the result of a write request to `geappliances/{device_id}/erd/0x{ERD}/write_result`. On success, the payload is `"ok"`. On failure, the payload is a JSON object: `{"error":"<reason>"}` where `<reason>` is one of `retries_exhausted`, `not_supported`, `incorrect_size`, or `unknown`. The message is published with retain flag set. Guards against null or disconnected MQTT client.

### 4.3 `on_write_request`

```c
i_tiny_event_t* on_write_request(i_mqtt_client_t* self);
```

Return a pointer to the `on_write_request_event` interface. The `erd_write_bridge` subscribes to this event to handle incoming write commands.

### 4.4 `on_mqtt_disconnect`

```c
i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* self);
```

Return a pointer to the `on_mqtt_disconnect_event` interface. Bridges subscribe to this to pause or defer operations until reconnection.

### 4.5 `on_mqtt_connect`

```c
i_tiny_event_t* on_mqtt_connect(i_mqtt_client_t* self);
```

Return a pointer to the `on_mqtt_connect_event` interface. Bridges subscribe to this to resume operations or re-publish state.

### 4.6 `publish_raw`

```c
void publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);
```

Forward to `esphome_mqtt_client_adapter_publish_raw()`. Creates a temporary `std::string` from the raw payload for the ESPHome MQTT client API. Guards against null or disconnected MQTT client.

### 4.7 `subscribe`

```c
void subscribe(
  i_mqtt_client_t* self,
  const char* topic,
  void (*callback)(const char* topic, const char* payload, size_t payload_len, void* arg),
  void* arg);
```

Register a callback for an MQTT topic. The adapter wraps the C function pointer in a C++ lambda and forwards it to `esphome::mqtt::global_mqtt_client->subscribe()`, passing QoS 0. When a message arrives on the topic, ESPHome invokes the lambda, which converts the `std::string` parameters to C strings and calls the original callback with the `arg` pointer. The callback and `arg` are captured by value in the lambda closure, which is heap-allocated by the ESPHome MQTT client and retained for the lifetime of the subscription.

Guards against null MQTT client: if `global_mqtt_client` is `nullptr` at the time of the call, the function returns immediately without registering the callback.

### 4.8 `unsubscribe`

```c
void unsubscribe(i_mqtt_client_t* self, const char* topic);
```

Remove a previously registered subscription for a topic. Forwards to `esphome::mqtt::global_mqtt_client->unsubscribe()`, which releases the lambda closure and tells the broker to stop delivering messages for that topic. Guards against null MQTT client: if `global_mqtt_client` is `nullptr`, the function returns immediately.

---

## 5. Subscribe/Unsubscribe Callback Lifecycle

### 5.1 Callback Storage

The adapter does not maintain its own collection of callbacks. When `subscribe()` is called, the C function pointer and `arg` are captured by value in a C++ lambda closure, which is passed to the ESPHome MQTT client. The ESPHome client stores the closure internally and is responsible for its lifetime. The adapter has no way to enumerate, modify, or prematurely destroy stored callbacks.

### 5.2 When Callbacks Fire

Callbacks fire when the MQTT broker delivers a message matching the subscribed topic. The ESPHome MQTT client receives the message on its internal task, invokes the stored lambda, which in turn calls the original C callback synchronously. The callback runs in the ESPHome MQTT task context, not the main loop.

### 5.3 Null Safety

Both `subscribe()` and `unsubscribe()` guard against a null `global_mqtt_client`:

- `subscribe()`: if `global_mqtt_client` is `nullptr`, returns immediately. The callback is not registered and will not fire. The caller is responsible for retrying subscription after the MQTT client becomes available (e.g., after `on_mqtt_connect` event).
- `unsubscribe()`: if `global_mqtt_client` is `nullptr`, returns immediately. No error is raised.

### 5.4 Multiple Subscriptions

The adapter supports multiple independent subscriptions to different topics. Each call to `subscribe()` registers a separate callback with the ESPHome MQTT client. Topics may use MQTT wildcards (`+` for single-level, `#` for multi-level). Calling `subscribe()` again for the same topic replaces the previous callback (ESPHome behavior). Calling `unsubscribe()` for a topic removes only that topic's subscription; other subscriptions remain active.

The `_self` parameter is unused in both `subscribe()` and `unsubscribe()` — the functions operate on the global MQTT client singleton rather than per-instance state. This means all adapter instances share the same subscription namespace on the MQTT client.

---

## 6. Wildcard Subscription

### 6.1 Design

Instead of subscribing to individual write topics for each ERD (e.g., `geappliances/{device_id}/erd/0x0001/write`, `geappliances/{device_id}/erd/0x0002/write`, ...), the adapter subscribes to a single wildcard topic:

```
geappliances/{device_id}/erd/+/write
```

The `+` wildcard matches any single topic level, covering all possible ERD identifiers.

### 6.2 Benefits

- Eliminates 100+ individual MQTT subscriptions (one per ERD)
- Eliminates 100+ heap-allocated lambda closures (one per subscription callback)
- Eliminates 100+ IDF MQTT outbox entries (SUBSCRIBE packets)
- Eliminates a ~3-second stall on MQTT reconnect caused by synchronous re-subscriptions

### 6.3 Write Command Routing
When a message arrives on the wildcard topic, the adapter:

1. Parses the ERD ID from the topic path (e.g., `0x7701` from `.../erd/0x7701/write`)
2. Decodes the hex-encoded payload into raw bytes (e.g., `"01"` → `0x01`, `"FF"` → `0xFF`)
3. Decodes the hex-encoded payload into a **local stack buffer** `local_buffer[32]` inside the lambda (not `write_payload_buffer_`). This avoids a race condition: if a new MQTT message arrives before `tiny_event_publish()` delivers the current one, the shared `write_payload_buffer_` would be overwritten. The local buffer is safe because `tiny_event_publish()` is synchronous.
4. Publishes `on_write_request_event` with the ERD ID, decoded size, and decoded data pointer

The `erd_write_bridge` receives the event and dispatches the write to the ERD client.

---

**Aspirational:** The pending update queue is not implemented in the current code. `drain_pending_updates()` returns 0, `get_pending_update_count()` returns 0, and there is no queueing, flush, or settle delay logic. The following describes the intended design for a future implementation.

### 7.1 Queue on Disconnect

When the MQTT client disconnects, ERD value updates would be queued rather than dropped. The queue would be keyed by ERD, so repeated updates to the same ERD overwrite the previous pending entry rather than appending. This ensures only the latest value is published after reconnection.

### 7.2 Flush on Reconnect

When `notify_connected()` is called, pending updates would be flushed to MQTT. To avoid stalling the main loop, updates would be drained in small batches (e.g., 5 per `drain_pending_updates()` call). The caller would be responsible for calling `drain_pending_updates()` repeatedly until it returns 0.

### 7.3 Settle Delay

After reconnect, pending updates would not be flushed immediately. A settle delay (tracked via `mqtt_connected_at_ms`) would give the IDF MQTT task time to process the broker's reconnect backlog before the adapter begins publishing. This prevents message ordering issues and reduces the chance of the MQTT outbox filling up.

### 7.4 Queue Capacity

The pending update queue would have a maximum capacity of 200 entries. If the queue is full, new updates would overwrite the oldest entry.

---

## 8. ERD Filtering

### 8.1 ErdRegistry Integration

The adapter holds an optional pointer to `ErdRegistry`. When set via `esphome_mqtt_client_adapter_set_erd_registry()`, the registry provides:

- **Valid-ERD filtering:** `ErdRegistry::is_valid()` checks whether an ERD is in the valid set populated by `FeatureBitManager` at startup. This prevents publishing ERDs that the appliance does not support.
- **Registered-ERD tracking:** `ErdRegistry::register_erd()` records which ERDs have been registered at runtime, used by diagnostics.
- **String-ERD type detection:** The registry can identify ERDs whose values are strings rather than binary, allowing appropriate payload encoding.

### 8.2 Filtering Behavior

- When `erd_registry` is `nullptr`, all ERDs pass through unfiltered.
- When `erd_registry` is non-null and `has_valid_erds_filter()` returns `true`, only ERDs in the valid set are processed.
- The filter is checked at publish time, not at registration time.

---
## 9. Invariants

1. **Single vtable instance:** The `i_mqtt_client_api_t` vtable is a file-scope `static const` variable. It is assigned once during `init()` and never modified.
2. **Wildcard subscription eliminates per-ERD overhead:** A single wildcard topic replaces per-ERD subscriptions, eliminating heap-allocated closures and MQTT outbox entries.
3. **Incoming hex decoding by adapter:** Incoming write payloads are received as hex strings and decoded to raw bytes inside the subscribe lambda before being passed to the write bridge. Outgoing hex encoding of ERD values is performed by the caller (e.g., `erd_bridge_subscribe`), not by the adapter.
4. **Fire-and-forget publishes:** Both `publish()` and `publish_raw()` guard against null/disconnected MQTT client and return void. They do not report success or failure.
5. **Event-driven architecture:** Write requests and connection state changes are communicated via `tiny_event_t` pub/sub, not direct callbacks. This allows multiple subscribers.
6. **Caller-owned struct:** The adapter struct is allocated by the caller (stack or static). `device_id` is a raw pointer to caller-owned memory (not heap-allocated by the adapter). `destroy()` nulls the pointer but does not free it.
7. **Null-safe operations:** All public methods guard against null `erd_registry` and null/disconnected MQTT client.

---

## 10. Dependencies

| Dependency | Purpose |
|------------|---------|
| `esphome::mqtt::global_mqtt_client` | ESPHome MQTT client singleton for publish and connection callbacks |
| `i_mqtt_client.h` | Abstract interface definition from tiny-gea-api |
| `tiny_event.h` | Event pub/sub system for write requests and connection events |
| `ErdRegistry` | Valid-ERD filtering, registered-ERD tracking, string-type detection |
| `tiny_erd.h` | `tiny_erd_t` type for ERD identifiers |
| `tiny_utils.h` | Utility functions for hex encoding |
| `esphome::core::log` | ESPHome logging framework (`ESP_LOGD`) |

---

## 11. Known Limitations

1. **No individual ERD write topics:** The wildcard subscription approach means write commands are received on a single topic and routed by parsing the ERD from the topic path. Individual ERD write topics are not subscribed to.
2. **No publish acknowledgment:** All publish operations are fire-and-forget. There is no QoS or acknowledgment mechanism exposed to the caller.
3. **Single write request event:** There is one `on_write_request` event shared across all ERDs. The bridge must filter by `erd` in the event args if it needs to handle specific ERDs differently.
4. **No pending queue in current implementation:** The pending update queue, drain, and settle delay are described as design decisions but not yet implemented in the current code. The adapter currently returns 0 for `get_pending_update_count()` and `drain_pending_updates()`.
5. **ESPHome MQTT client singleton dependency:** The adapter depends on `esphome::mqtt::global_mqtt_client` being non-null and properly initialized before any publish operation. There is no fallback or alternative transport.
