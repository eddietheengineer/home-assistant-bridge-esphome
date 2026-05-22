# MQTT Bridge (Subscription Mode)

## Purpose

Manages the GEA3 ERD subscription lifecycle in subscription mode. Subscribes to appliance ERD publications, retains the subscription every 30 seconds, and publishes received ERD values via MQTT. Handles write requests from MQTT and forwards them to the appliance.

## Public API

| Function | Description |
|----------|-------------|
| `mqtt_bridge_init(self, timer_group, erd_client, mqtt_client, address)` | Initialize the subscription bridge |
| `mqtt_bridge_destroy(self)` | Unsubscribe events, stop timers, free heap state |

## State Machine

```
sub_state_top (parent state — handles publication and write signals globally)
  ├─ state_subscribing
  │    ├─ entry: clear ERD set
  │    ├─ entry / timer_expired / subscription_failed: attempt subscribe()
  │    ├─ subscription_added_or_retained → state_subscribed
  │    └─ exit: disarm timer
  │
  └─ state_subscribed
       ├─ entry: arm periodic timer (30 s retention)
       ├─ timer_expired: retain subscription
       ├─ subscription_host_came_online / mqtt_disconnected → state_subscribing
       └─ exit: disarm timer
```

Shared signals (handled in `mqtt_bridge_common.h`):
- `signal_subscription_publication_received` — publish ERD value to MQTT
- `signal_write_requested` — forward write to ERD client
- `signal_mqtt_disconnected` — transition back to subscribing

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client with subscription support
- `i_mqtt_client` — MQTT client adapter
- `tiny_hsm` — hierarchical state machine
- `tiny_timer` — periodic retention timer
- `mqtt_bridge_common.h` — shared signals, timing constants, and utility templates

## Key Design Decisions

- **ERD set tracking**: A `std::set<tiny_erd_t>` tracks which ERDs have been registered with the MQTT client. On entry to `state_subscribing` (e.g., after reconnect), the set is cleared so ERDs are re-registered — this matters because the MQTT broker may have dropped subscriptions during disconnect.
- **30-second retention**: The subscription is retained every 30 seconds (`subscription_retention_period`) to keep the appliance publishing ERD values. If retention fails, the bridge transitions back to `state_subscribing`.
- **1-second resubscribe delay**: If `subscribe()` fails, the bridge waits 1 second (`resubscribe_delay`) before retrying.
- **Clean destroy**: All three event subscriptions (ERD activity, write request, MQTT disconnect) are unsubscribed before freeing the ERD set, preventing use-after-free if events fire after destroy.

## Testing

Covered by unit tests in `test/tests/test_mqtt_bridge.cpp` and integration tests through the full bridge subscription flow. The state machine transitions are tested with simulated ERD client activity events.
