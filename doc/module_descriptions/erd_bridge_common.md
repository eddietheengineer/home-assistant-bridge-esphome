# ERD Bridge Common

## Purpose

Header-only shared utilities used by both the subscription bridge (`erd_bridge_subscribe.cpp`) and the polling bridge (`erd_bridge_poll.cpp`). All functions are either template functions (implicitly inline) or declared `static inline` so that each translation unit gets its own copy without ODR violations.

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
| `signal_timer_expired` | Both | Generic timer fired (retry timer in subscription, retention timer in subscription) |
| `signal_polling_timer_expired` | Polling only | Polling cycle interval timer fired |
| `signal_subscription_failed` | Subscription only | `subscribe()` call failed |
| `signal_subscription_added_or_retained` | Subscription only | Subscription was added or retained successfully |
| `signal_subscription_host_came_online` | Subscription only | Appliance host restarted and came back online |
| `signal_subscription_publication_received` | Subscription only | ERD publication received via subscription |
| `signal_read_failed` | Polling only | ERD read request failed |
| `signal_read_completed` | Polling only | ERD read request completed successfully |
| `signal_appliance_lost` | Polling only | 60 s appliance-lost timer fired |

## Fixed-Capacity ERD Set

The `erd_set_t` struct replaces the previous `std::set<tiny_erd_t>` to eliminate heap node allocations. It is a sorted array with binary search, providing O(log n) lookups and O(n) inserts (n is small: bounded by probe list or subscription ERDs).

| Field | Type | Description |
|-------|------|-------------|
| `data` | `tiny_erd_t[ERD_SET_CAPACITY]` | Sorted array of ERD identifiers |
| `count` | `uint16_t` | Number of entries in the set |

`ERD_SET_CAPACITY` is 645, matching `POLLING_LIST_MAX_SIZE`.

### API

| Function | Description |
|----------|-------------|
| `erd_set_init(self)` | Initialize an empty set (sets `count` to 0) |
| `erd_set_contains(self, erd)` | Binary search; returns `true` if the ERD is present |
| `erd_set_insert(self, erd)` | Inserts at the correct sorted position; returns `false` if already present or capacity exceeded |
| `erd_set_clear(self)` | Resets `count` to 0 (does not zero the data array) |

## Utility Templates

### `arm_timer(T* self, tiny_timer_ticks_t ticks)`

Starts a one-shot timer that sends `signal_timer_expired` to the HSM when it fires. Template parameter `T` is the bridge type (`erd_bridge_subscribe_t` or `erd_bridge_poll_t`).

### `disarm_timer(T* self)`

Stops the timer. Safe to call even if the timer is not active.

### `erd_set(T* self)`

Returns a reference to the `erd_set_t` stored directly in the bridge struct. Used by both bridges to track registered ERDs.

## Dependencies

- `tiny_hsm` — state machine framework (for `tiny_hsm_send_signal`)
- `tiny_timer` — timer framework (for `tiny_timer_start`, `tiny_timer_stop`)
- `tiny_erd` — ERD type definition
- `tiny_utils` — utility functions
- `tiny_gea_constants` — GEA protocol constants

## Key Design Decisions

- **Header-only with inline**: All functions are template or `static inline`, avoiding ODR violations when both `erd_bridge_subscribe.cpp` and `erd_bridge_poll.cpp` include this header.
- **Shared signal namespace**: Both bridges use the same signal ID range (starting from `tiny_hsm_signal_user_start`) to avoid conflicts. Each bridge's HSM only receives signals relevant to its own state machine.
- **Fixed-capacity ERD set**: Replaced `std::set<tiny_erd_t>` with a sorted array (`erd_set_t`) to eliminate per-ERD heap allocations. This was a critical fix — the previous `std::set` caused heap fragmentation and memory pressure on the ESP32. Binary search keeps lookups fast (O(log n)) for the small set sizes involved.
- **No MQTT dependency**: This header does not reference `i_mqtt_client`, `i_tiny_gea3_erd_client`, or `<set>`. Write handling and MQTT disconnect handling have been extracted to `erd_write_bridge` and `erd_cache_mqtt_publisher` respectively.
- **No heap allocation**: The `erd_set_t` struct is embedded directly in each bridge struct — no dynamic allocation, no `void*` casting.

## Testing

Exercised indirectly through the unit tests for `erd_bridge_subscribe` and `erd_bridge_poll`. The shared utilities themselves are not tested in isolation.
