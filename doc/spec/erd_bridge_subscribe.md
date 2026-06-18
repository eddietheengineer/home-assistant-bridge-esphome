# ERD Bridge Subscribe — Specification

## 1. Overview

### 1.1 Purpose

The ERD subscription bridge manages the GEA3 ERD subscription lifecycle: it subscribes to an appliance at a specific bus address, retains the subscription periodically, and publishes received ERD values to the shared ERD cache. It has **zero** direct interaction with the MQTT client.

### 1.2 Responsibilities

- Subscribe to ERD publications from a specific appliance address
- Retain the active subscription at a fixed interval
- Forward received ERD publications to the shared ERD cache
- Recover from appliance restart (re-subscribe)
- Recover from subscription failure (retry with delay)

### 1.3 Not Responsible For

- Polling-mode operation (see `erd_bridge_poll`)
- Write request handling (see `erd_write_bridge`)
- Any MQTT behavior (publishing, write requests, disconnect handling)
- Bridge startup phase management (see `geappliances_bridge_startup_hsm`)

---

## 2. Initialization

### 2.1 Primary Init

```c
void erd_bridge_subscribe_init(
    erd_bridge_subscribe_t* self,
    tiny_timer_group_t* timer_group,
    i_tiny_gea3_erd_client_t* erd_client,
    uint8_t address,
    erd_cache_t* cache);
```

| Parameter | Description |
|-----------|-------------|
| `timer_group` | Shared timer group for the retention timer. |
| `erd_client` | GEA3 ERD client interface with subscription support. |
| `address` | The appliance's GEA bus address (e.g., `0xC0`, `0xC4`). The bridge subscribes only to this address and filters out events from other addresses. |
| `cache` | Shared `erd_cache_t` for storing ERD values. |

### 2.2 Destroy

```c
void erd_bridge_subscribe_destroy(erd_bridge_subscribe_t* self);
```

Stops the retention timer, unsubscribes all event handlers, and frees the heap-allocated `erd_set`. Guards against being called on a never-initialized struct (checks `timer_group == NULL`).

---

## 3. State Machine

The subscription bridge uses a hierarchical state machine (`tiny_hsm`) with a parent state (`sub_state_top`) and two child states.

### 3.1 Parent State: `sub_state_top`

Handles the `signal_subscription_publication_received` signal regardless of the current child state:

| Signal | Behavior |
|--------|----------|
| `signal_subscription_publication_received` | If the ERD is not already in `erd_set`, inserts it. Calls `erd_cache_update()` with `is_subscription = true`. |

### 3.2 Child States

#### `state_subscribing`

Initial state. Attempts to establish or re-establish the subscription.

**On `tiny_hsm_signal_entry`:**
- Calls `tiny_gea3_erd_client_subscribe(erd_client, erd_host_address)`.
- If subscribe fails: arms the resubscribe timer for `resubscribe_delay` (1000 ms).

**On `signal_subscription_host_came_online`:**
- The appliance host restarted — its ERD set may have changed.
- Clears `erd_set` and resets `erd_cache` via `erd_cache_init()`.
- Falls through to the subscribe attempt below.

**On `signal_timer_expired`:**
- Resubscribe timer fired after a failed subscribe.
- Retries `tiny_gea3_erd_client_subscribe()`.
- If subscribe fails again: re-arms the resubscribe timer.

**On `signal_subscription_failed`:**
- Subscribe call returned false (queue full).
- Retries `tiny_gea3_erd_client_subscribe()`.
- If subscribe fails again: arms the resubscribe timer.

**On `signal_subscription_added_or_retained`:**
- Subscription is active.
- Transitions to `state_subscribed`.

**On `tiny_hsm_signal_exit`:**
- Disarms the resubscribe timer.

#### `state_subscribed`

Steady state. Retains the subscription periodically and processes publications.

**On `tiny_hsm_signal_entry`:**
- Arms the retention timer for `subscription_retention_period` (30000 ms).

**On `signal_timer_expired`:**
- Calls `tiny_gea3_erd_client_retain_subscription(erd_client, erd_host_address)`.
- If retain fails, the ERD client will fire `signal_subscription_failed`, which is deferred to `state_subscribing` (see §3.3).

**On `signal_subscription_host_came_online`:**
- Appliance restarted.
- Transitions to `state_subscribing`.

**On `tiny_hsm_signal_exit`:**
- Disarms the retention timer.

### 3.3 Signal Routing

Signals not handled by the current child state are deferred to the parent (`sub_state_top`). The parent handles `signal_subscription_publication_received` in all states. Signals handled by `state_subscribing` but not `state_subscribed` (e.g., `signal_subscription_failed`) cause an implicit transition to `state_subscribing` when the ERD client fires them while in `state_subscribed`.

### 3.4 State Diagram

```
sub_state_top (parent — handles publication_received globally)
  ├─ state_subscribing (initial)
  │    ├─ entry / timer_expired / subscription_failed → subscribe()
  │    ├─ subscription_host_came_online → clear erd_set, erd_cache, then subscribe()
  │    ├─ subscription_added_or_retained → state_subscribed
  │    └─ exit → disarm timer
  │
  └─ state_subscribed
       ├─ entry → arm retention timer (30 s)
       ├─ timer_expired → retain_subscription()
       ├─ subscription_host_came_online → state_subscribing
       └─ exit → disarm timer
```

---

## 4. Address Filtering

The event subscription callback filters all ERD client activity events by `erd_host_address`:

```c
if (args->address != self->erd_host_address) {
    return;
}
```

This enables **multiple independent subscription bridge instances** sharing the same ERD client and cache, each targeting a different appliance address. Events from appliance A (`0xC0`) are delivered only to the bridge instance initialized with `address = 0xC0`; events from appliance B (`0xC4`) are delivered only to the bridge instance initialized with `address = 0xC4`.

---

## 5. ERD Set

- `erd_set`: `std::set<tiny_erd_t>` stored as `void*` in the struct.
- Tracks which ERDs have been seen in subscription publications.
- Cleared only on `signal_subscription_host_came_online` (appliance restart).
- **NOT** cleared on transient subscription failures or MQTT reconnects.

---

## 6. Data Structures

```c
typedef struct {
    tiny_timer_group_t* timer_group;
    i_tiny_gea3_erd_client_t* erd_client;
    tiny_timer_t timer;
    tiny_event_subscription_t erd_client_activity_subscription;
    void* erd_set;
    erd_cache_t* erd_cache;
    tiny_hsm_t hsm;
    uint8_t erd_host_address;
} erd_bridge_subscribe_t;
```

---

## 7. Timing Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `subscription_retention_period` | 30000 ms | Interval between subscription retention requests. |
| `resubscribe_delay` | 1000 ms | Delay before retrying a failed subscription. |

---

## 8. Invariants

1. **One subscription at a time:** The bridge subscribes to exactly one appliance address. Multiple bridge instances can be created for multiple appliances.
2. **Address isolation:** Each bridge instance processes only events matching its `erd_host_address`.
3. **ERD set only cleared on appliance restart:** The `erd_set` is cleared on `signal_subscription_host_came_online`, not on transient failures.
4. **Cache cleared on appliance restart:** `erd_cache_init()` is called when the appliance host comes online, clearing stale data before re-subscribing.
5. **Clean destroy:** All event subscriptions are removed before freeing the `erd_set`, preventing use-after-free if events fire after destroy.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `i_tiny_gea3_erd_client` | GEA3 ERD client interface (subscribe, retain_subscription, activity events) |
| `tiny_hsm` | Hierarchical state machine |
| `tiny_timer` | Timer group and timer instances |
| `erd_bridge_common.h` | Shared signals, timing constants, and utility templates (`erd_set`, `arm_timer`, `disarm_timer`) |
| `erd_cache.h` | ERD cache for storing values (`erd_cache_init`, `erd_cache_update`) |

---

## 10. Known Limitations

1. **Single appliance per instance:** Each subscription bridge instance subscribes to exactly one appliance address. Supporting multiple appliances requires multiple bridge instances.
2. **No write handling:** The subscription bridge does not handle write requests. Write handling is the responsibility of `erd_write_bridge`.
3. **No discovery:** The subscription bridge does not discover which ERDs an appliance supports. It accepts all publications from the subscribed address and adds them to the cache.
4. **Cache cleared on appliance restart:** When the appliance host comes online, the entire shared ERD cache is reset. If another bridge (e.g., polling) shares the cache, its data is also cleared.
