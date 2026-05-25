# HaDiscoveryManager

## Purpose

Manages Home Assistant MQTT autodiscovery: watches for a "ready" signal (quiet window in subscription mode or polling list complete in polling mode), spawns a FreeRTOS background task to fetch JSONL entity definitions via HTTPS, parses them into MQTT discovery payloads, and publishes them with rate limiting.

## Public API

| Method | Description |
|--------|-------------|
| `init(base_url, device_id, model_number, serial_number, registered_erds, generate_device_config)` | Initialize with device info and ERD list |
| `set_registered_erds(erds)` | Update the registered ERD set |
| `on_erd_seen(erd)` | Track a new ERD seen (resets quiet window timer) |
| `run(is_poll_mode, polling_list_complete, subscription_activity_detected, mqtt_client)` | Drive state machine; called every `loop()` |
| `is_complete()` | Returns `true` when HA discovery is finished |
| `is_failed()` | Returns `true` when HA discovery has failed |
| `is_publishing()` | Returns `true` when currently publishing entities |
| `is_ready_to_start()` | Returns `true` when waiting for ready signal |
| `get_state()` | Returns the current `HaDiscoveryState` |
| `cleanup()` | Free FreeRTOS task, queue, and stack resources |

## State Machine

```
HA_DISCOVERY_IDLE
  → HA_DISCOVERY_WAITING_FOR_READY
    ├─ poll mode: ready when polling_list_complete
    └─ subscription mode: ready after 10s quiet window (or 30s safety cap)
      → HA_DISCOVERY_DOWNLOADING (spawn FreeRTOS task)
        → HA_DISCOVERY_PUBLISHING (rate-limited at 50ms per entity)
          → HA_DISCOVERY_COMPLETE (all entities published)

HA_DISCOVERY_FAILED (terminal — heap allocation or task creation failed)
```

Constants:
- `HA_DISCOVERY_QUIET_MS = 10000` — quiet window in subscription mode
- `HA_DISCOVERY_MAX_WAIT_MS = 30000` — safety cap to prevent indefinite blocking
- `HA_ENTITY_PUBLISH_INTERVAL_MS = 50` — rate limit between entity publishes
- `HA_FETCH_MIN_FREE_HEAP = 110 KB` — minimum free heap required to spawn fetch task
- `HA_FETCH_STACK_SIZE = 49152` — stack size for the fetch task

## Dependencies

- ESP-IDF `esp_http_client` — HTTPS fetch of JSONL definitions
- ESP-IDF `cJSON` — JSON parsing of JSONL lines
- FreeRTOS `xTaskCreateStatic` — background fetch task
- ESPHome `mqtt::MQTTClientComponent` — publishing discovery messages
- `tiny_gea3_erd_client` — ERD type definitions

## Key Design Decisions

- **Background fetch task**: The HTTPS fetch runs in a dedicated FreeRTOS task (48 KB stack) to avoid blocking the ESPHome main loop. The task communicates with the main loop via a FreeRTOS queue.
- **Heap safety checks**: Before spawning the fetch task, the manager checks that free heap > 110 KB and largest free block > 48 KB. If not, it skips discovery gracefully.
- **Category-based fetching**: Only fetches JSONL categories that contain ERDs the device actually registers (10 categories: common, refrigeration, laundry, dishwasher, waterheater, range, airconditioning, waterfilter, smallappliance, energy).
- **Rate-limited publishing**: Entities are published at 50 ms intervals to avoid overwhelming the MQTT broker or triggering rate limits.
- **ESP-IDF only**: On non-ESP-IDF builds (e.g., tests), the fetch is a no-op and a warning is logged.
- **Entity filtering**: JSONL entities are filtered against the registered ERD snapshot — only entities for ERDs the device actually supports are published. Paired ERDs (request/response) are handled by checking both the entity's ERD and its paired ERD.

## Testing

Covered by integration tests in `test/tests/` on non-ESP-IDF platforms (where the fetch is a no-op). The state machine transitions and ready-signal logic are tested through the full startup sequence.
