# DeviceIdentityManager

## Purpose

Reads appliance identity ERDs (type, model number, serial number) and assembles a unique MQTT-topic-safe device identifier string. If a `device_id` is pre-configured in YAML, the read sequence is skipped entirely.

## Public API

| Method | Description |
|--------|-------------|
| `init(configured_id, erd_client, host_address)` | Initialize with optional pre-configured ID, ERD client, and host address |
| `run()` | Attempt to queue the next ERD read; called every `loop()` iteration |
| `on_erd_read_completed(erd, data, size)` | Called when an ERD read succeeds |
| `on_erd_read_failed(erd)` | Called when an ERD read fails; retries up to 3 times then uses fallback |
| `is_complete()` | Returns `true` when device ID is ready |
| `is_failed()` | Returns `true` when device ID generation has failed |
| `get_device_id()` | Returns the final device ID string |
| `get_appliance_type()` | Returns the appliance type byte |
| `get_generated_device_id()` | Returns the auto-generated ID (before fallback) |
| `get_model_number()` | Returns the model number string |
| `get_serial_number()` | Returns the serial number string |
| `get_state()` | Returns the current `DeviceIdState` |
| `get_queue_retry_count()` | Returns the current queue retry count |

## State Machine

```
DEVICE_ID_STATE_IDLE
  └─ if configured_id is set → DEVICE_ID_STATE_COMPLETE (skip reads)

DEVICE_ID_STATE_READING_APPLIANCE_TYPE  (ERD 0x0008)
  └─ success → DEVICE_ID_STATE_IDLE (queue next)
  └─ fail (3 retries) → fallback to 0xFF, continue to next

DEVICE_ID_STATE_READING_MODEL_NUMBER    (ERD 0x0001)
  └─ success → DEVICE_ID_STATE_IDLE (queue next)
  └─ fail (3 retries) → fallback to "Unknown", continue to next

DEVICE_ID_STATE_READING_SERIAL_NUMBER   (ERD 0x0002)
  └─ success → assemble ID → DEVICE_ID_STATE_COMPLETE
  └─ fail (3 retries) → fallback to "Unknown", assemble ID → COMPLETE

DEVICE_ID_STATE_FAILED (terminal — max queue retries exceeded)
```

Device ID format: `{appliance_type_string}_{sanitized_model}_{sanitized_serial}`

Constants:
- `MAX_QUEUE_RETRIES = 1000` — max attempts to queue a read when the ERD client queue is full
- `MAX_DEVICE_ID_RESPONSE_RETRIES = 3` — max retries per ERD read before using fallback value
- `LOG_EVERY_N_RETRIES = 50` — log interval for queue retry spam

## Dependencies

- `tiny_gea3_erd_client` — ERD client interface (used for both GEA3 and GEA2 via adapter)
- `appliance_type_to_string()` — generated function that maps appliance type byte to string
- `geappliances_bridge_constants.h` — ERD constants

## Key Design Decisions

- **MQTT topic sanitization**: Characters `+`, `#`, `/`, `$`, spaces, and non-printable bytes are replaced with `_` to ensure the device ID is safe for MQTT topics.
- **Graceful degradation**: Each ERD read has independent retry logic — if one ERD fails, the manager continues with fallback values rather than aborting the entire sequence.
- **Queue retry handling**: The ERD client queue can be full during busy bus conditions; the manager retries up to 1000 times before giving up, logging every 50 attempts.

## Testing

Covered by integration tests in `test/tests/` through the full startup sequence. The retry and fallback logic is tested via simulated ERD read failures.
