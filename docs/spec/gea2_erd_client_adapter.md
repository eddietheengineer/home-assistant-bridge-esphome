# GEA2 ERD Client Adapter Specification

## Overview

The adapter wraps a GEA2 ERD client (`i_tiny_gea2_erd_client_t`) and presents it as a GEA3 ERD client (`i_tiny_gea3_erd_client_t`). This allows the polling bridge, write bridge, and any other GEA3-aware component to operate over a GEA2 bus without modification.

### Purpose

Provide a transparent GEA2-to-GEA3 interface shim so the rest of the bridge stack is protocol-agnostic.

### Responsibilities

- Delegate `read()` and `write()` calls to the underlying GEA2 client.
- Translate GEA2 request IDs to GEA3 request IDs (uint8_t cast).
- Re-publish GEA2 activity events as GEA3 activity events via the adapter's own `tiny_event`.
- Report that subscriptions are not supported (`subscribe()` always returns `false`; `retain_subscription()` always returns `false`).

### Not Responsible For

- Implementing subscription support — GEA2 does not support it.
- Transforming ERD data, addresses, or event payloads.
- Managing the lifecycle of the underlying GEA2 client.
- Scheduling or retrying reads/writes.

## Public API

```c
void gea2_erd_client_adapter_init(
  gea2_erd_client_adapter_t* self,
  i_tiny_gea2_erd_client_t* gea2_client);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the adapter struct (zero-initialized by caller) |
| `gea2_client` | GEA2 ERD client to wrap |

Initialization sets up the GEA3 API vtable, stores the GEA2 client pointer, initializes the adapter's activity event, and subscribes to the GEA2 client's activity event for re-publishing.

### `gea2_erd_client_adapter_destroy(self, gea2_client)`

Unsubscribes the adapter from the GEA2 client's activity event. Guards against null `self`, null `gea2_client`, and mismatched client pointer. Idempotent — safe to call multiple times. Sets `self->gea2_client` to `nullptr` after unsubscribe.

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the adapter struct (may be null) |
| `gea2_client` | GEA2 client that was passed to `init()` (may be null) |

## i_tiny_gea3_erd_client API Implementation

The adapter implements `i_tiny_gea3_erd_client_api_t` with the following callbacks:

### `adapter_read(self, request_id, address, erd)`

Delegates to `tiny_gea2_erd_client_read(gea2_client, &gea2_id, address, erd)`. The GEA2 request ID is cast to `tiny_gea3_erd_client_request_id_t` and written to `*request_id` if non-null. Returns the GEA2 result directly.

### `adapter_write(self, request_id, address, erd, data, data_size)`

Delegates to `tiny_gea2_erd_client_write(gea2_client, &gea2_id, address, erd, data, data_size)`. The GEA2 request ID is cast to `tiny_gea3_erd_client_request_id_t` and written to `*request_id` if non-null. Returns the GEA2 result directly.

### `adapter_subscribe(self, address)`

Always returns `false`. GEA2 does not support subscriptions; callers should fall back to polling.

### `adapter_retain_subscription(self, address)`

Always returns `false`. GEA2 does not support subscriptions.

### `adapter_on_activity(self)`

Returns a pointer to the adapter's `on_activity` event interface. Callers subscribe to this event to receive GEA2 activity events presented as GEA3 activity events.

## Layout-Compatible Activity Args

GEA2 and GEA3 activity argument structs are layout-compatible for the event types used by the polling and write bridges:

| Type | Event | Layout Compatible |
|------|-------|-------------------|
| 0 | `read_completed` | Yes |
| 1 | `read_failed` | Yes |
| 2 | `write_completed` | Yes |
| 3 | `write_failed` | Yes |

The adapter re-publishes GEA2 activity args directly to its `on_activity` event without transformation. The `on_gea2_activity()` callback receives `const void* args` from the GEA2 client and passes it straight to `tiny_event_publish(&self->on_activity, args)`.

## Request ID Translation

GEA2 uses `tiny_gea2_erd_client_request_id_t` (uint8_t). GEA3 uses `tiny_gea3_erd_client_request_id_t` (uint8_t). The adapter casts the GEA2 ID to the GEA3 type with no transformation — both are `uint8_t`. This is safe because:

- The polling bridge correlates completion/failure events by request ID.
- The write bridge validates request IDs against `pending_request_id`.
- Both types have identical representation.

## Data Structures

```c
typedef struct {
  i_tiny_gea3_erd_client_t interface;
  i_tiny_gea2_erd_client_t* gea2_client;
  tiny_event_t on_activity;
  tiny_event_subscription_t gea2_sub;
} gea2_erd_client_adapter_t;
```

All members are embedded — no heap allocation. The struct is stack-allocated or embedded in the bridge.

## Invariants

1. **No subscription support:** `subscribe()` always returns `false`; `retain_subscription()` always returns `false`. The adapter never attempts to subscribe with the GEA2 client.
2. **Transparent to polling bridge:** Read requests and their completions/failures pass through without modification. The polling bridge sees the adapter as a normal GEA3 client.
3. **Event re-publish without transformation:** GEA2 activity events are forwarded to the adapter's `on_activity` event with the raw args pointer — no field conversion, no copy.
4. **Single GEA2 client:** The adapter wraps exactly one GEA2 client for its lifetime. There is no reconfiguration API.
5. **No memory allocation:** The adapter uses only stack-embedded state.

## Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea2_erd_client.h` | GEA2 ERD client interface (delegated to) |
| `i_tiny_gea3_erd_client.h` | GEA3 ERD client interface (presented by adapter) |
| `tiny_event.h` | Event infrastructure for re-publishing activity |

## Known Limitations

1. **No subscription support:** GEA2 does not support ERD subscriptions. The adapter always returns `false` for `subscribe()`, forcing callers to use polling. This is a protocol limitation, not an implementation gap.
2. **Request ID cast assumes compatible types:** The adapter casts `uint8_t` GEA2 request IDs to GEA3 request IDs. If either type changes in the future, the adapter must be updated.
3. **No error translation:** GEA2 failure reasons are passed through as-is. If GEA2 and GEA3 use different failure reason enums, the caller may see unexpected values.
4. **No reconfiguration:** Once initialized, the adapter cannot be pointed at a different GEA2 client. Re-initialization would require re-subscribing to the new client's activity event.
