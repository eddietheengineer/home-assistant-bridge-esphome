# MQTT Bridge Common

## Purpose

Header-only shared utilities used by both the subscription bridge (`mqtt_bridge.cpp`) and the polling bridge (`mqtt_bridge_polling.cpp`). All functions are either template functions (implicitly inline) or declared `inline` so that each translation unit gets its own copy without ODR violations.

## Shared Timing Constants

Defined as an anonymous `enum` (compile-time constants, no runtime storage):

| Constant | Value | Description |
|----------|-------|-------------|
| `resubscribe_delay` | 1000 ms | Wait before retrying `subscribe()` after failure (subscription bridge) |
| `subscription_retention_period` | 30000 ms (30 s) | Interval for retaining subscriptions (subscription bridge) |
| `retry_delay` | 100 ms | Timeout for individual ERD read retries (both bridges) |
| `appliance_lost_timeout` | 60000 ms (60 s) | Time without a successful read before declaring appliance lost (polling bridge) |

## Shared Signal Identifiers

Signal IDs shared between both bridge state machines, defined as an anonymous `enum` starting from `tiny_hsm_signal_user_start`:

| Signal | Used By | Description |
|--------|---------|-------------|
| `signal_timer_expired` | Both | Generic timer fired (retry timer in polling, retention timer in subscription) |
| `signal_polling_timer_expired` | Polling only | Polling cycle interval timer fired |
| `signal_subscription_failed` | Subscription only | `subscribe()` call failed |
| `signal_subscription_added_or_retained` | Subscription only | Subscription was added or retained successfully |
| `signal_subscription_host_came_online` | Subscription only | Appliance host restarted and came back online |
| `signal_subscription_publication_received` | Subscription only | ERD publication received via subscription |
| `signal_read_failed` | Polling only | ERD read request failed |
| `signal_read_completed` | Polling only | ERD read request completed successfully |
| `signal_mqtt_disconnected` | Both | MQTT broker connection lost |
| `signal_appliance_lost` | Polling only | 60 s appliance-lost timer fired |
| `signal_write_requested` | Both | Write request received from MQTT |

## Utility Templates

### `arm_timer(T* self, tiny_timer_ticks_t ticks)`

Starts a one-shot timer that sends `signal_timer_expired` to the HSM when it fires. Template parameter `T` is the bridge type (`mqtt_bridge_t` or `mqtt_bridge_polling_t`).

### `disarm_timer(T* self)`

Stops the timer. Safe to call even if the timer is not active.

### `erd_set(T* self)`

Returns a reference to the `std::set<tiny_erd_t>` stored in the bridge's `erd_set` member (which is a `void*` in the C struct). Used by both bridges to track registered ERDs.

### `handle_write_result(i_mqtt_client_t* mqtt_client, const tiny_gea3_erd_client_on_activity_args_t* args)`

Non-template function that handles write completion/failure events. Updates the MQTT client with the write result (success/failure + reason code). Used by both bridges in their ERD activity handlers.

### `setup_write_request_subscription(T* self, i_mqtt_client_t* mqtt_client)`

Subscribes to the MQTT client's `on_write_request` event and forwards it as `signal_write_requested` to the HSM. Used during bridge initialization.

### `setup_disconnect_subscription(T* self, i_mqtt_client_t* mqtt_client)`

Subscribes to the MQTT client's `on_mqtt_disconnect` event and forwards it as `signal_mqtt_disconnected` to the HSM. Critically, this handler does **NOT** clear `erd_set` — clearing it on every transient MQTT reconnect causes heap fragmentation (N std::set tree nodes freed/reallocated per cycle) and polling list growth (in api_parsed mode, all ERDs get re-added on each reconnect). The `erd_set` is only cleared in `state_add_common_erds` (full-discovery path).

## Dependencies

- `tiny_hsm` — state machine framework (for `tiny_hsm_send_signal`)
- `tiny_timer` — timer framework (for `tiny_timer_start`, `tiny_timer_stop`)
- `tiny_event` — event pub/sub framework (for subscription setup)
- `i_mqtt_client` — MQTT client interface
- `i_tiny_gea3_erd_client` — GEA3 ERD client interface (for write result handling)
- `<set>` — `std::set` for ERD tracking

## Key Design Decisions

- **Header-only with inline**: All functions are template or inline, avoiding ODR violations when both `mqtt_bridge.cpp` and `mqtt_bridge_polling.cpp` include this header.
- **Shared signal namespace**: Both bridges use the same signal ID range (starting from `tiny_hsm_signal_user_start`) to avoid conflicts. Each bridge's HSM only receives signals relevant to its own state machine.
- **Disconnect handler preserves erd_set**: The `setup_disconnect_subscription` callback intentionally does not clear the ERD set. This was a critical fix — the previous behavior of clearing on every disconnect caused heap fragmentation and polling list growth over time.
- **Void* for C++ types in C structs**: The C struct definitions use `void*` for C++ types (`std::set`, `std::map`) to maintain C compatibility. The template helpers in this header safely cast them back.

## Testing

Exercised indirectly through the unit tests for `mqtt_bridge` and `mqtt_bridge_polling`. The shared utilities themselves are not tested in isolation.
