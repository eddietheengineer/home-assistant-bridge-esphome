# GEA2 ERD Client Adapter

## Purpose

Wraps a GEA2 ERD client (`i_tiny_gea2_erd_client_t`) and presents it as a GEA3 ERD client (`i_tiny_gea3_erd_client_t`). This allows the existing polling bridge (which requires `i_tiny_gea3_erd_client_t`) to operate over a GEA2 bus without modification.

## Public API

| Function | Description |
|----------|-------------|
| `gea2_erd_client_adapter_init(self, gea2_client)` | Initialize adapter, wrapping the GEA2 client |

## Internal i_tiny_gea3_erd_client API Implementation

The adapter implements the `i_tiny_gea3_erd_client_api_t` interface:

| Callback | Description |
|----------|-------------|
| `adapter_read(request_id, address, erd)` | Delegate to GEA2 client's `tiny_gea2_erd_client_read` |
| `adapter_write(request_id, address, erd, data, size)` | Delegate to GEA2 client's `tiny_gea2_erd_client_write` |
| `adapter_subscribe(address)` | Always returns `false` (GEA2 does not support subscriptions) |
| `adapter_retain_subscription(address)` | No-op (GEA2 does not support subscriptions) |
| `adapter_on_activity()` | Return the adapter's event interface |

## Dependencies

- `i_tiny_gea2_erd_client` — GEA2 ERD client interface
- `i_tiny_gea3_erd_client` — GEA3 ERD client interface (presented by adapter)
- `tiny_event` — GEA2 activity events are re-published through the adapter's GEA3-typed event

## Key Design Decisions

- **Layout-compatible activity args**: GEA2 and GEA3 `on_activity` args are layout-compatible for read/write event types (types 0–3), so GEA2 activity args can be re-published through the adapter's GEA3-typed event without data transformation.
- **No subscription support**: `subscribe()` and `retain_subscription()` always return `false`, forcing callers to use polling mode when operating over GEA2.
- **Request ID translation**: GEA2 request IDs are cast to GEA3 request IDs (both are `uint16_t`), with no transformation needed.

## Testing

Covered by integration tests in `test/tests/` through the GEA2 autodiscovery and polling paths. The adapter is transparent to the polling bridge — no adapter-specific tests are needed.
