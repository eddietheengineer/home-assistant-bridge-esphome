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
- Queue ERD updates during MQTT disconnect and flush on reconnect with a settle delay
- Delegate ERD registration tracking and valid-ERD filtering to `ErdRegistry`
- Provide a `publish()` helper for HA discovery manager to send arbitrary MQTT messages

### 1.3 Not Responsible For

- Deciding which ERDs to publish (filtering is applied via `ErdRegistry`)
- Managing bridge lifecycle or startup phases
- HA discovery publishing (`HaDiscoveryManager`)
- ERD value serialization or string conversion (handled at the HA discovery level)
- MQTT connection management (connect, disconnect, reconnection — handled by ESPHome)

---

## 2. Data Structures

### 2.1 Adapter struct

```c
typedef struct {
  i_mqtt_client_t interface;
  std::string* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  tiny_event_t on_mqtt_connect_event;
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
} esphome_mqtt_client_adapter_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `interface` | `i_mqtt_client_t` | Handle wrapping the vtable pointer; set during `init()` |
| `device_id` | `std::string*` | Heap-allocated device ID string; used in topic construction |
| `on_write_request_event` | `tiny_event_t` | Event published when a write command arrives via MQTT |
| `on_mqtt_disconnect_event` | `tiny_event_t` | Event published when the MQTT client disconnects |
| `on_mqtt_connect_event` | `tiny_event_t` | Event published when the MQTT client connects |
| `erd_registry` | `ErdRegistry*` | Optional pointer; when non-null, provides valid-ERD filtering, string-ERD type detection, and registered-ERD tracking |

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

Initialize the adapter. Allocates a `std::string` copy of `device_id` on the heap. Initializes the three `tiny_event_t` instances. Wires ESPHome MQTT client connect/disconnect callbacks to the adapter's notify functions. If the MQTT client is already connected at the time of init, fires the connect event immediately so downstream publishers see the correct state on their first `loop()`.

**Parameters:**
- `self` — Pointer to uninitialized adapter struct (stack or static allocation; caller-owned)
- `device_id` — Null-terminated device ID string (copied; caller retains ownership of the original)

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

Publish the `on_mqtt_connect_event` to notify subscribers that MQTT is up. Called automatically by the ESPHome MQTT client's connect callback. In the full implementation, this also triggers flushing the pending update queue after a settle delay.

### 3.5 `esphome_mqtt_client_adapter_subscribe_write_topic`

```c
void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self);
```

Subscribe to the wildcard write topic `geappliances/{device_id}/erd/+/write`. In the current implementation this is a no-op (no MQTT broker available in test/debug mode). In the full implementation, this subscribes once to the wildcard topic, eliminating the need for per-ERD subscriptions.

### 3.6 `esphome_mqtt_client_adapter_drain_pending_updates`

```c
size_t esphome_mqtt_client_adapter_drain_pending_updates(
  esphome_mqtt_client_adapter_t* self);
```

Flush pending ERD updates accumulated during a disconnect. Returns the number of updates drained. In the current implementation this is a no-op returning 0. In the full implementation, updates are flushed in batches (e.g., 5 per call) to avoid stalling the main loop.

### 3.7 `esphome_mqtt_client_adapter_destroy`

```c
void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);
```

Free heap-allocated members. Deletes `device_id` and nulls the pointer. Safe to call multiple times.

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

Publish an MQTT message using `std::string` arguments. Used by `HaDiscoveryManager` for discovery payload publishing. Guards against null or disconnected MQTT client.

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

The adapter implements all six vtable slots of `i_mqtt_client_api_t`:

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

---

## 5. Wildcard Subscription

### 5.1 Design

Instead of subscribing to individual write topics for each ERD (e.g., `geappliances/{device_id}/erd/0x0001/write`, `geappliances/{device_id}/erd/0x0002/write`, ...), the adapter subscribes to a single wildcard topic:

```
geappliances/{device_id}/erd/+/write
```

The `+` wildcard matches any single topic level, covering all possible ERD identifiers.

### 5.2 Benefits

- Eliminates 100+ individual MQTT subscriptions (one per ERD)
- Eliminates 100+ heap-allocated lambda closures (one per subscription callback)
- Eliminates 100+ IDF MQTT outbox entries (SUBSCRIBE packets)
- Eliminates a ~3-second stall on MQTT reconnect caused by synchronous re-subscriptions

### 5.3 Write Command Routing

When a message arrives on the wildcard topic, the adapter parses the ERD from the topic path and publishes the `on_write_request_event` with the appropriate `mqtt_client_on_write_request_args_t`. The `erd_write_bridge` receives the event and dispatches the write to the ERD client.

---

## 6. Pending Update Queue

### 6.1 Queue on Disconnect

When the MQTT client disconnects, ERD value updates are queued rather than dropped. The queue is keyed by ERD, so repeated updates to the same ERD overwrite the previous pending entry rather than appending. This ensures only the latest value is published after reconnection.

### 6.2 Flush on Reconnect

When `notify_connected()` is called, pending updates are flushed to MQTT. To avoid stalling the main loop, updates are drained in small batches (e.g., 5 per `drain_pending_updates()` call). The caller is responsible for calling `drain_pending_updates()` repeatedly until it returns 0.

### 6.3 Settle Delay

After reconnect, pending updates are not flushed immediately. A settle delay (tracked via `mqtt_connected_at_ms`) gives the IDF MQTT task time to process the broker's reconnect backlog before the adapter begins publishing. This prevents message ordering issues and reduces the chance of the MQTT outbox filling up.

### 6.4 Queue Capacity

The pending update queue has a maximum capacity of 200 entries. If the queue is full, new updates overwrite the oldest entry.

---

## 7. ERD Filtering

### 7.1 ErdRegistry Integration

The adapter holds an optional pointer to `ErdRegistry`. When set via `esphome_mqtt_client_adapter_set_erd_registry()`, the registry provides:

- **Valid-ERD filtering:** `ErdRegistry::is_valid()` checks whether an ERD is in the valid set populated by `FeatureBitManager` at startup. This prevents publishing ERDs that the appliance does not support.
- **Registered-ERD tracking:** `ErdRegistry::register_erd()` records which ERDs have been registered at runtime, used by `HaDiscoveryManager` and diagnostics.
- **String-ERD type detection:** The registry can identify ERDs whose values are strings rather than binary, allowing appropriate payload encoding.

### 7.2 Filtering Behavior

- When `erd_registry` is `nullptr`, all ERDs pass through unfiltered.
- When `erd_registry` is non-null and `has_valid_erds_filter()` returns `true`, only ERDs in the valid set are processed.
- The filter is checked at publish time, not at registration time.

---

## 8. Invariants

1. **Single vtable instance:** The `i_mqtt_client_api_t` vtable is a file-scope `static const` variable. It is assigned once during `init()` and never modified.
2. **Wildcard subscription eliminates per-ERD overhead:** A single wildcard topic replaces per-ERD subscriptions, eliminating heap-allocated closures and MQTT outbox entries.
3. **Hex payloads:** All ERD values are published as uppercase hex strings. String conversion is handled at the HA discovery level, not in the MQTT adapter.
4. **Settle delay after reconnect:** Pending updates are not flushed immediately after reconnect; a delay ensures the MQTT transport is ready.
5. **Fire-and-forget publishes:** Both `publish()` and `publish_raw()` guard against null/disconnected MQTT client and return void. They do not report success or failure.
6. **Event-driven architecture:** Write requests and connection state changes are communicated via `tiny_event_t` pub/sub, not direct callbacks. This allows multiple subscribers.
7. **Caller-owned struct:** The adapter struct is allocated by the caller (stack or static). Only `device_id` is heap-allocated by `init()` and freed by `destroy()`.
8. **Null-safe operations:** All public methods guard against null `erd_registry` and null/disconnected MQTT client.

---

## 9. Dependencies

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

## 10. Known Limitations

1. **No individual ERD write topics:** The wildcard subscription approach means write commands are received on a single topic and routed by parsing the ERD from the topic path. Individual ERD write topics are not subscribed to.
2. **No publish acknowledgment:** All publish operations are fire-and-forget. There is no QoS or acknowledgment mechanism exposed to the caller.
3. **Single write request event:** There is one `on_write_request` event shared across all ERDs. The bridge must filter by `erd` in the event args if it needs to handle specific ERDs differently.
4. **No pending queue in current implementation:** The pending update queue, drain, and settle delay are described as design decisions but not yet implemented in the current code. The adapter currently returns 0 for `get_pending_update_count()` and `drain_pending_updates()`.
5. **No write topic subscription in current implementation:** `subscribe_write_topic()` is a no-op in the current implementation. Write command handling requires the full MQTT broker integration.
6. **Heap allocation for device_id:** The device ID is heap-allocated via `new std::string()`. If `destroy()` is never called, this leaks memory.
7. **ESPHome MQTT client singleton dependency:** The adapter depends on `esphome::mqtt::global_mqtt_client` being non-null and properly initialized before any publish operation. There is no fallback or alternative transport.
