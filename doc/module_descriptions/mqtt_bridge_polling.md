# MQTT Bridge Polling

## Purpose

Discovers the connected appliance by reading ERD 0x0008 (appliance type) on the broadcast address, then walks through a chain of per-appliance ERD discovery states before settling into steady-state polling. Publishes ERD values to MQTT and handles write requests.

## Public API

| Function | Description |
|----------|-------------|
| `mqtt_bridge_polling_init(self, timer_group, erd_client, mqtt_client, interval_ms, only_publish_on_change)` | Initialize with broadcast discovery |
| `mqtt_bridge_polling_init_at_address(self, timer_group, erd_client, mqtt_client, interval_ms, only_publish_on_change, known_address, api_list, api_list_count)` | Initialize with a pre-known host address (skips broadcast) |
| `mqtt_bridge_polling_destroy(self)` | Stop timers, unsubscribe events, free heap state |

## State Machine

```
poll_state_top (parent — handles write requests and appliance loss globally)
  ├─ state_identify_appliance
  │    └─ read ERD 0x0008 from broadcast (or skip if address pre-known)
  │       → if api_parsed_list set → state_add_appliance_api_feature_erds
  │       → else → state_add_common_erds
  │       (on re-entry with known address + api_parsed_list: clear erd_set,
  │        pending_registration_set, and polling list before transitioning)
  │
  ├─ state_add_common_erds
  │    └→ state_add_energy_erds
  │
  ├─ state_add_energy_erds
  │    └→ state_add_appliance_api_feature_erds
  │
  ├─ state_add_appliance_api_feature_erds
  │    └→ state_add_appliance_erds (or state_polling if api_parsed_list)
  │
  ├─ state_add_appliance_erds
  │    └→ state_polling
  │
  └─ state_polling (steady state)
       ├─ polling_timer_expired: restart cycle only when all ERDs have
       │   completed (success or failure) AND the timer has expired;
       │   on the first cycle (erd_index == polling_list_count), the
       │   timer kicks off the first read from ERD[0]
       ├─ read_completed: register on MQTT if deferred (first read for
       │   api_parsed_list or custom_erd_list ERDs), then publish if changed
       │   (or always), then read next ERD
       ├─ read_failed: count as completed, then read next ERD
       ├─ timer_expired (retry): re-arm (ERD client handles retries internally)
       ├─ mqtt_disconnected: continue polling (values are queued)
       └─ appliance_lost (60 s timeout) → state_identify_appliance
```

Discovery states use a shared `handle_discovery_list_signals` handler that reads each ERD in the list, adds it to the polling list, publishes the value, and transitions to the next state when done.

## Dependencies

- `i_tiny_gea3_erd_client` — GEA3 ERD client interface
- `i_mqtt_client` — MQTT client adapter
- `tiny_hsm` — hierarchical state machine
- `tiny_timer` — polling, retry, and appliance-lost timers
- `erd_lists.h` — static ERD lists (common, energy, appliance-type-specific)
- `mqtt_bridge_common.h` — shared signals, timing constants, and utility templates

## Key Design Decisions

- **Dynamic polling list**: The `erd_polling_list` is heap-allocated and grows in increments of 32 ERDs (up to `POLLING_LIST_MAX_SIZE`). This avoids fixed-size buffer limitations while bounding memory usage.
- **ERD cache for "publish on change"**: When `only_publish_on_change` is true, a `std::map<tiny_erd_t, vector<uint8_t>>` caches the last published value per ERD. Only changed values are published to MQTT.
- **Sequential polling — one read at a time**: Each ERD read is sent only after
  the previous one has completed (success or failure).  Forward progress is
  driven by `signal_read_completed` and `signal_read_failed`, each calling
  `send_next_poll_read_request()` for the next ERD.  The retry timer
  (`signal_timer_expired`) re-arms without resending — the ERD client handles
  retries internally (10 × 250ms).
- **Cycle restarts only when complete AND timer expired**: A new polling cycle
  (resetting `erd_index` to 0) starts only when **all** ERDs in the current
  cycle have either read successfully or failed **and** the polling timer
  (default 10 s) has expired.  This prevents overlapping cycles from building
  up pressure in the shared GEA3 ERD client queue.
- **No overlapping polling cycles**: Because only one read is ever in-flight
  and cycles don't restart until complete, the GEA3 ERD client's fixed-size
  request queue cannot overflow — preventing heap corruption that previously
  manifested as FreeRTOS `prvCheckTasksWaitingTermination` crashes.
- **60-second appliance lost timer**: If no read completes within 60 seconds, the bridge transitions back to `state_identify_appliance` to rediscover the appliance.
- **MQTT disconnect tolerance**: On MQTT disconnect, the polling bridge continues polling — values are queued in `pending_updates` and flushed when MQTT reconnects. No re-identification is needed.
- **API-parsed list shortcut**: When `api_parsed_list` is set (from appliance API feature bit parsing), the bridge skips common/energy/appliance discovery states and goes directly to polling with the parsed list.
- **Custom ERD support**: User-configured custom ERDs are appended to the polling list after discovered or API-parsed ERDs.
- **Deferred ERD registration**: ERDs added via `api_parsed_list` or `custom_erd_list` are placed in the polling list *without* being registered on MQTT initially.  Registration is deferred until the first successful read — confirming the ERD is actually present on the appliance.  ERDs are tracked in a `pending_registration_set` and removed from it once registered.  This avoids registering ERDs that may not exist on the specific appliance variant.
- **Re-entry after appliance lost**: When the bridge re-enters `state_polling` after the appliance-lost timer fires (e.g., with `init_at_address`), the `erd_set`, `pending_registration_set`, and polling list are cleared in `state_identify_appliance` before transitioning back to `state_polling`, so all ERDs are re-added via the deferred path and re-registered on first read.

## Testing

Covered by unit tests in `test/tests/test_mqtt_bridge_polling.cpp` and integration tests through the full polling bridge flow. Discovery state transitions, ERD caching, and appliance loss recovery are tested with simulated ERD client activity.
