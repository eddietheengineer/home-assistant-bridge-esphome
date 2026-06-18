# ERD Write Bridge — Design Specification

## Goal

Provide a dedicated component that routes write requests from MQTT to the GEA3 ERD client and reports write results back, without coupling the polling or subscription bridges to the MQTT client.

## Problem

Currently both `erd_bridge_poll` and `erd_bridge_subscribe` subscribe to `mqtt_client_on_write_request` and forward writes to the ERD client. This couples the bridges to the MQTT client, violating the principle that bridges should be pure data sources (read → cache).

After removing MQTT dependencies from the bridges, there is a gap: nothing handles write requests. This document defines the `erd_write_bridge` that fills that gap.

## Architecture

```
MQTT broker
    ↓  (write request on geappliances/{deviceId}/erd/0x{ERD}/set)
i_mqtt_client_t  ──on_write_request──▶  erd_write_bridge
                                               ↓
                                         i_tiny_gea3_erd_client_t  ──write──▶  GEA bus
                                               ↑
                                         on_activity (write_completed / write_failed)
                                               ↓
                                         erd_write_bridge  ──publish──▶  i_mqtt_client_t  ──publish_raw──▶  MQTT broker
```

The write bridge is a thin relay: it receives write commands from MQTT, sends them to the ERD client, and reports the result back to MQTT. It has no discovery logic, no polling, no subscription management.

## Responsibilities

- Subscribe to `mqtt_client_on_write_request` event
- Forward the write to the ERD client via `tiny_gea3_erd_client_write()`
- On `write_completed` or `write_failed` from the ERD client, publish the result back to MQTT via `mqtt_client_update_erd_write_result()`
- Gate writes on appliance identification (host address must not be the broadcast address)
- Handle MQTT disconnect gracefully (queue writes or drop with log)

## Not Responsible For

- ERD discovery or polling
- Subscription management
- ERD value publishing
- Bridge startup or lifecycle management

## Public API

```c
void erd_write_bridge_init(
    erd_write_bridge_t* self,
    tiny_timer_group_t* timer_group,
    i_tiny_gea3_erd_client_t* erd_client,
    i_mqtt_client_t* mqtt_client,
    uint8_t host_address);

void erd_write_bridge_destroy(erd_write_bridge_t* self);

void erd_write_bridge_set_host_address(erd_write_bridge_t* self, uint8_t host_address);
```

| Parameter | Description |
|-----------|-------------|
| `timer_group` | Shared timer group (used for retry timers if needed) |
| `erd_client` | GEA3 ERD client interface |
| `mqtt_client` | MQTT client interface (subscribe to write requests, publish results) |
| `host_address` | The appliance's GEA bus address. Writes are dropped if this is the broadcast address (appliance not yet identified). |

### `erd_write_bridge_set_host_address`

Updates the host address after the appliance is identified or after a re-identification event. Called by `GeappliancesBridge` after autodiscovery completes or after appliance loss recovery.

## State Machine

The write bridge uses a flat state machine with two states:

```
state_ready (initial)
  └─ on write_requested:
      if host_address == broadcast:
          log warning, drop request, publish failure result
      else:
          send write to ERD client
          transition to state_writing

state_writing
  └─ on write_completed:
      publish success result to MQTT
      transition to state_ready
  └─ on write_failed:
      publish failure result to MQTT
      transition to state_ready
```

## Data Structures

```c
typedef struct {
    tiny_timer_group_t* timer_group;
    i_tiny_gea3_erd_client_t* erd_client;
    i_mqtt_client_t* mqtt_client;
    uint8_t erd_host_address;
    tiny_hsm_t hsm;
    tiny_event_subscription_t mqtt_write_request_subscription;
    tiny_event_subscription_t erd_client_activity_subscription;
    // Pending write state (one write at a time)
    tiny_gea3_erd_client_request_id_t pending_request_id;
    tiny_erd_t pending_erd;
} erd_write_bridge_t;
```

## Write Flow

1. MQTT client receives a write request (e.g., from Home Assistant)
2. `mqtt_client_on_write_request` fires with `mqtt_client_on_write_request_args_t` (erd, value, size)
3. Write bridge receives the signal in `state_ready`
4. If `erd_host_address == tiny_gea_broadcast_address`: log warning, call `mqtt_client_update_erd_write_result(erd, false, ...)` to report failure, stay in `state_ready`
5. Otherwise: call `tiny_gea3_erd_client_write(erd_client, &request_id, erd_host_address, erd, value, size)`, transition to `state_writing`
6. ERD client fires `write_completed` or `write_failed` activity event
7. Write bridge receives the event in `state_writing`, calls `mqtt_client_update_erd_write_result(erd, success, reason)`, transitions back to `state_ready`

## Integration with GeappliancesBridge

### Initialization

```cpp
// In GeappliancesBridge::initialize_erd_bridge_() or similar:
erd_write_bridge_init(
    &this->erd_write_bridge_,
    &this->timer_group_,
    this->autodiscovery_manager_.get_active_erd_client(),
    &this->mqtt_client_adapter_.interface,
    tiny_gea_broadcast_address);  // Initially unknown
```

### Host Address Update

After autodiscovery identifies the appliance:

```cpp
// In GeappliancesBridge after host address is known:
erd_write_bridge_set_host_address(
    &this->erd_write_bridge_,
    this->autodiscovery_manager_.get_host_address());
```

After appliance loss recovery:

```cpp
// In GeappliancesBridge after re-identification:
erd_write_bridge_set_host_address(
    &this->erd_write_bridge_,
    this->autodiscovery_manager_.get_host_address());
```

### Teardown

```cpp
// In GeappliancesBridge::teardown():
erd_write_bridge_destroy(&this->erd_write_bridge_);
```

## MQTT Topics

The write bridge publishes results to the same topic pattern used by the existing `mqtt_client_update_erd_write_result()`:

| Result | Topic |
|--------|-------|
| Success | `geappliances/{deviceId}/erd/0x{ERD}/set` with payload `"ok"` |
| Failure | `geappliances/{deviceId}/erd/0x{ERD}/set` with payload `"error:{reason}"` |

The exact topic and payload format is determined by the `i_mqtt_client_t::update_erd_write_result` implementation in the MQTT adapter.

## Concurrency

Only one write is processed at a time. If a write request arrives while a previous write is in progress (`state_writing`), the new request is dropped with a warning log. The ERD client itself may queue the write internally, but the bridge does not track multiple pending writes.

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Appliance not identified (broadcast address) | Drop write, publish failure result |
| Write request during in-progress write | Drop with warning log |
| ERD client write fails (queue full) | Publish failure result, return to `state_ready` |
| ERD client reports `not_supported` | Publish failure result with reason |
| ERD client reports `retries_exhausted` | Publish failure result with reason |
| ERD client reports `incorrect_size` | Publish failure result with reason |
| MQTT disconnected | Continue processing; write results are queued by the MQTT adapter |

## Testing

Unit tests verify:
- Write is forwarded to ERD client when host address is known
- Write is dropped when host address is broadcast
- Write result (success/failure) is published back to MQTT
- Second write during in-progress write is dropped
- Host address update enables writes after identification
- MQTT disconnect does not crash the bridge

## Future Considerations

- **Write queue:** If multiple writes arrive rapidly, a queue could buffer them instead of dropping. This adds complexity but improves reliability.
- **Write timeout:** A timer could fire if the ERD client does not respond within a configurable window, transitioning back to `state_ready` and publishing a timeout failure.
- **Batch writes:** If the ERD client supports batch writes, the bridge could accumulate writes and send them together.
