# FeatureBitManager

## Purpose

Reads and parses appliance API feature bit ERDs (0x0092 through 0x010D), building a filtered list of valid ERDs that the appliance supports. This list is used by the polling bridge to only poll ERDs that are actually registered, and by the MQTT adapter to filter published values.

## Public API

| Method | Description |
|--------|-------------|
| `init(erd_client, host_address, mqtt_client, mqtt_initialized)` | Initialize with ERD client, host address, and optional MQTT client |
| `run()` | Queue next ERD read or perform deferred parsing; called every `loop()` |
| `on_erd_read_completed(erd, data, size)` | Store raw ERD data and queue next read |
| `on_erd_read_failed(erd)` | Skip to next ERD in sequence |
| `is_complete()` | Returns `true` when all ERDs read and parsed |
| `is_failed()` | Returns `true` when feature bit reading has failed |
| `is_parse_pending()` | Returns `true` when parsing is pending (deferred to `loop()`) |
| `get_valid_erds()` | Returns the set of valid ERDs built from feature bits |
| `get_valid_erds_vec()` | Returns the valid ERDs as a sorted vector (for C API) |
| `is_valid_list_ready()` | Returns `true` when the valid ERD list is finalized |
| `get_erd_data()` | Returns raw ERD data for legacy sync |
| `get_state()` | Returns the current `FeatureBitState` |

## State Machine

```
FEATURE_BIT_STATE_READING_0008  (appliance type, re-read)
  → FEATURE_BIT_STATE_READING_0001  (model number, re-read)
    → FEATURE_BIT_STATE_READING_0002  (serial number, re-read)
      → FEATURE_BIT_STATE_READING_0092  (common feature API)
        → FEATURE_BIT_STATE_READING_0093  (appliance feature API 0)
          → ... (0094, 0095, 0096, 0097, 0109, 010A, 010B, 010C, 010D)
            → FEATURE_BIT_STATE_COMPLETE
              → parse_pending_ = true
                  → FEATURE_BIT_STATE_COMPLETE (parsing in loop())
                      → valid_list_ready_ = true

Any read failure → skip to next ERD in sequence
Queue full (1000 retries) → skip to next ERD
```

Parsing is incremental — one ERD per `loop()` call — to avoid triggering ESPHome's 30 ms loop watchdog (the full parse can take 1+ seconds).

## Dependencies

- `tiny_gea3_erd_client` — ERD client interface
- `i_mqtt_client` — optional, for publishing raw feature bit values
- `appliance_api_feature_lists.h` — generated descriptor tables for feature bit parsing
- `geappliances_bridge_constants.h` — ERD constants

## Key Design Decisions

- **Deferred parsing**: The `parse_pending_` flag is set when all ERDs are read, but actual parsing happens incrementally in `run()` (one ERD per `loop()` call). This prevents blocking the ESPHome loop for too long.
- **Queue retry tolerance**: Each ERD read is retried up to 1000 times if the queue is full, then skipped. This handles busy bus conditions without stalling the entire sequence.
- **Mandatory ERDs**: The final valid ERD list always includes the feature bit ERDs themselves (0x0092–0x010D) plus identity ERDs (0x0001, 0x0002, 0x0008), regardless of feature bit values.
- **Common features first**: ERD 0x0092 (common features) is parsed first, then appliance-specific ERDs (0x0093–0x010D) are matched against descriptor tables by appliance type and version.

## Testing

Covered by integration tests in `test/tests/` through the full startup sequence. The parsing logic is tested against known feature bit patterns for various appliance types.
