# AutodiscoveryManager

## Purpose

Manages the appliance autodiscovery process on the GEA bus. After a 5-second startup delay, it broadcasts ERD 0x0008 (appliance type) requests to discover connected GEA3 or GEA2 appliance boards, with retry logic and protocol fallback.

## Public API

| Method | Description |
|--------|-------------|
| `init(gea3_client, gea2_client, gea2_adapter, has_gea3, has_gea2, on_complete_cb)` | Initialize with ERD clients, UART availability flags, and completion callback |
| `run()` | Drive the state machine forward; called every `loop()` iteration |
| `on_broadcast_response(address, appliance_type, is_gea3)` | Called when a broadcast response is received from an appliance |
| `is_complete()` | Returns `true` when autodiscovery has finished (success or failure) |
| `is_failed()` | Returns `true` when autodiscovery completed with no appliance found |
| `get_host_address()` | Returns the discovered appliance address |
| `get_active_erd_client()` | Returns the ERD client for the discovered protocol |
| `is_gea2_protocol()` | Returns `true` if a GEA2 appliance was discovered |
| `get_retry_count()` | Returns the number of retries attempted |
| `get_state()` | Returns the current `AutodiscoveryState` |

## State Machine

```
AUTODISCOVERY_WAITING_5S
  └─ after 5 s delay →
    AUTODISCOVERY_GEA3_BROADCAST_PENDING → AUTODISCOVERY_GEA3_BROADCAST_WAITING
      ├─ response received → AUTODISCOVERY_COMPLETE
      └─ timeout → retry indefinitely → GEA2 fallback or re-try GEA3

    AUTODISCOVERY_GEA2_BROADCAST_PENDING → AUTODISCOVERY_GEA2_BROADCAST_WAITING
      ├─ response received → AUTODISCOVERY_COMPLETE (gea2_protocol_active = true)
      └─ timeout → retry indefinitely → GEA3 fallback or re-try GEA2

AUTODISCOVERY_COMPLETE (terminal state — only reached on successful discovery)
```

Constants:
- `AUTODISCOVERY_STARTUP_DELAY_MS = 5000` — initial delay before first broadcast
- `AUTODISCOVERY_BROADCAST_WINDOW_MS = 5000` — wait window for response
## Dependencies

- `tiny_gea3_erd_client` — GEA3 ERD client interface
- `tiny_gea2_erd_client` — GEA2 ERD client interface
- `esphome::millis()` — timing
- `geappliances_bridge_constants.h` — `GEA_BROADCAST_ADDRESS`, `ERD_APPLIANCE_TYPE`

## Key Design Decisions

- **Infinite retries**: The manager never gives up — if no board responds, it keeps retrying indefinitely, alternating between GEA3 and GEA2 (if both UARTs are configured). The startup HSM will not transition past the autodiscovery phase until a valid board is discovered. This prevents the bridge from proceeding with an invalid `host_address_ = 0x00`.
- **Protocol fallback**: If both GEA3 and GEA2 UARTs are configured, failure to find a GEA3 board falls back to GEA2 (and vice versa) before retrying the same protocol.
- **Single response wins**: The first valid broadcast response sets the host address; subsequent responses are ignored.
- **Completion callback**: The `on_complete_cb` is invoked once when transitioning to `AUTODISCOVERY_COMPLETE`, allowing the bridge to proceed to device ID generation.

## Testing

Covered by integration tests in `test/tests/` that exercise the full startup sequence. The state machine transitions are tested implicitly through the bridge's autodiscovery flow.
