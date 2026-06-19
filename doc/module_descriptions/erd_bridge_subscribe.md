# ERD Bridge (Subscription Mode)

## Purpose

Manages the GEA3 ERD subscription lifecycle in subscription mode. Subscribes to appliance ERD publications, retains the subscription every 30 seconds, and publishes received ERD values to the shared ERD cache. Write handling and MQTT disconnect handling are delegated to `erd_write_bridge` and `erd_cache_mqtt_publisher` respectively.

## Public API

| Function | Description |
|----------|-------------|
| `erd_bridge_subscribe_init(self, timer_group, erd_client, address, cache)` | Initialize the subscription bridge |
| `erd_bridge_subscribe_destroy(self)` | Unsubscribe events, stop timers (no heap cleanup needed) |

## State Machine

```
sub_state_top (parent state — handles publication signals globally)
  ├─ state_subscribing
  │    ├─ entry / timer_expired / subscription_failed: attempt subscribe()
  │    ├─ subscription_host_came_online: clear ERD set, then attempt subscribe()
  │    ├─ subscription_added_or_retained → state_subscribed
  │    └─ exit: disarm timer
  │
  └─ state_subscribed
       ├─ entry: arm periodic timer (30 s retention)
       ├─ timer_expired: retain subscription
       ├─ subscription_host_came_online → state_subscribing
       └─ exit: disarm timer
```

Shared signals (handled in `erd_bridge_common.h`):
- `signal_subscription_publication_received` — publish ERD value to ERD cache

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client with subscription support
- `tiny_hsm` — hierarchical state machine
- `tiny_timer` — periodic retention timer
- `erd_bridge_common.h` — shared signals, timing constants, and utility templates
- `erd_cache.h` — shared ERD cache for publishing values

## Key Design Decisions

- **ERD set tracking**: An `erd_set_t` (fixed-capacity sorted array) tracks which ERDs have been seen via subscription publications. The set is cleared on `signal_subscription_host_came_online` (appliance restart) so that new publications are re-registered.
- **No MQTT dependency**: The bridge writes to `erd_cache` only. MQTT publishing is handled by `erd_cache_mqtt_publisher`. Write handling is delegated to `erd_write_bridge`.
- **No `signal_mqtt_disconnected`**: MQTT disconnect handling has been moved to `erd_cache_mqtt_publisher`. The subscription bridge does not react to MQTT disconnects.
- **No `signal_write_requested`**: Write request handling has been extracted to `erd_write_bridge`.
- **30-second retention**: The subscription is retained every 30 seconds (`subscription_retention_period`) to keep the appliance publishing ERD values. If retention fails, the bridge transitions back to `state_subscribing`.
- **1-second resubscribe delay**: If `subscribe()` fails, the bridge waits 1 second (`resubscribe_delay`) before retrying.
- **Clean destroy**: The event subscription is unsubscribed before the struct is freed, preventing use-after-free if events fire after destroy. All state is embedded in the struct — no heap cleanup needed.

## Testing

Covered by unit tests in `test/tests/erd_bridge_subscribe_test.cpp` and integration tests through the full bridge subscription flow. The state machine transitions are tested with simulated ERD client activity events.
