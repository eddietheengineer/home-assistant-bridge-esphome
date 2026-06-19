# ERD Bridge Subscribe Specification

## Overview

The subscription bridge manages the GEA3 ERD subscription lifecycle. It subscribes to all ERD publications from a single appliance address, retains the subscription periodically, and writes received ERD values to the shared ERD cache. It has no dependency on MQTT — write handling and MQTT disconnect handling are delegated to `erd_write_bridge` and `erd_cache_mqtt_publisher` respectively.

## Public API

```c
void erd_bridge_subscribe_init(
  erd_bridge_subscribe_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  uint8_t address,
  erd_cache_t* cache);

void erd_bridge_subscribe_destroy(erd_bridge_subscribe_t* self);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the bridge struct (zero-initialized by caller) |
| `timer_group` | Shared timer group for retention timer |
| `erd_client` | GEA3 ERD client with subscription support |
| `address` | Appliance host address (not broadcast) |
| `cache` | Shared ERD cache — updated with each received publication |

## Struct Layout

```c
typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  tiny_timer_t timer;
  tiny_event_subscription_t erd_client_activity_subscription;
  erd_set_t erd_set;
  erd_cache_t* erd_cache;
  tiny_hsm_t hsm;
  uint8_t erd_host_address;
} erd_bridge_subscribe_t;
```

All members are stack-allocated or embedded — no heap allocation.

## State Machine

```
sub_state_top (parent — handles publication signals globally)
  ├─ state_subscribing (initial)
  │    ├─ entry: attempt subscribe()
  │    ├─ subscription_host_came_online: clear ERD set, then attempt subscribe()
  │    ├─ subscription_failed / timer_expired: attempt subscribe()
  │    ├─ subscription_added_or_retained → state_subscribed
  │    └─ exit: disarm timer
  │
  └─ state_subscribed
       ├─ entry: arm periodic timer (30 s retention)
       ├─ timer_expired: retain subscription
       ├─ subscription_host_came_online → state_subscribing
       └─ exit: disarm timer
```

### `sub_state_top` (Parent)

Handles `signal_subscription_publication_received` globally across all child states:
- Inserts the ERD into `erd_set` (if not already present)
- Updates the ERD cache with the received data

### `state_subscribing` (Initial)

- On entry: calls `tiny_gea3_erd_client_subscribe()` with the host address
- On `signal_subscription_host_came_online`: clears `erd_set` (appliance may have changed its ERD set), then attempts subscribe
- On `signal_subscription_failed` or `signal_timer_expired`: attempts subscribe with `resubscribe_delay` (1 s) backoff
- On `signal_subscription_added_or_retained`: transitions to `state_subscribed`
- On exit: disarms the retry timer

### `state_subscribed` (Steady State)

- On entry: arms a periodic retention timer at `subscription_retention_period` (30 s)
- On `signal_timer_expired`: calls `tiny_gea3_erd_client_retain_subscription()` to keep the appliance publishing
- On `signal_subscription_host_came_online`: transitions back to `state_subscribing`
- On exit: disarms the retention timer

## Event Subscription

The bridge subscribes to `tiny_gea3_erd_client_on_activity` during `init()`. The callback filters events by `address == erd_host_address` and routes them to HSM signals:

| Activity Type | HSM Signal |
|---|---|
| `subscription_added_or_retained` | `signal_subscription_added_or_retained` |
| `subscription_publication_received` | `signal_subscription_publication_received` |
| `subscription_host_came_online` | `signal_subscription_host_came_online` |
| `subscribe_failed` | `signal_subscription_failed` |
| `write_completed` / `write_failed` | Ignored (handled by `erd_write_bridge`) |

## ERD Set

The `erd_set_t` is a fixed-capacity sorted array (capacity 645). It tracks which ERDs have been seen via subscription publications. It is cleared on `signal_subscription_host_came_online` (appliance restart) so that new publications are re-registered.

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client with subscription support
- `tiny_hsm` — hierarchical state machine
- `tiny_timer` — periodic retention timer
- `erd_bridge_common.h` — shared signals, timing constants, and utility templates
- `erd_cache.h` — shared ERD cache for publishing values

## Key Design Decisions

- **No MQTT dependency**: The bridge writes to `erd_cache` only. MQTT publishing is handled by `erd_cache_mqtt_publisher`. Write handling is delegated to `erd_write_bridge`.
- **No `signal_mqtt_disconnected`**: MQTT disconnect handling has been moved to `erd_cache_mqtt_publisher`. The subscription bridge does not react to MQTT disconnects.
- **No `signal_write_requested`**: Write request handling has been extracted to `erd_write_bridge`.
- **Fixed-capacity ERD set**: Uses `erd_set_t` (sorted array) instead of `std::set` to eliminate heap node allocations.
- **30-second retention**: The subscription is retained every 30 seconds (`subscription_retention_period`) to keep the appliance publishing ERD values.
- **1-second resubscribe delay**: If `subscribe()` fails, the bridge waits 1 second (`resubscribe_delay`) before retrying.
- **Clean destroy**: The event subscription is unsubscribed before the struct is freed, preventing use-after-free if events fire after destroy.
- **Cache NOT cleared on host restart**: The ERD cache is not reset during `signal_subscription_host_came_online`. The cache may be shared with the polling bridge; stale entries are overwritten when new data arrives.

## Testing

Covered by unit tests in `test/tests/erd_bridge_subscribe_test.cpp` and integration tests through the full bridge subscription flow. The state machine transitions are tested with simulated ERD client activity events.
