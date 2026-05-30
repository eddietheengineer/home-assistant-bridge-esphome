# FeatureBitManager

## Purpose

Reads and parses appliance API feature bit ERDs (0x0092 through 0x010D), building a filtered list of valid ERDs that the appliance supports. Fully self-driving: owns its own timers and event subscriptions. This list is used by the polling bridge to only poll ERDs that are actually registered, and by the MQTT adapter to filter published values.

## Public API

| Method | Description |
|--------|-------------|
| `init(erd_client, host_address)` | Initialize with ERD client, host address |
| `on_erd_read_completed(erd, data, size)` | Store raw ERD data and queue next read |
| `on_erd_read_failed(erd)` | Skip to next ERD in sequence |
| `get_valid_erds()` | Returns the set of valid ERDs built from feature bits |
| `get_valid_erds_vec()` | Returns the valid ERDs as a sorted vector (for C API) |
| `get_state()` | Returns the current `FeatureBitState` |

## State Machine

```
FEATURE_BIT_STATE_READING_0008  (appliance type, re-read)
  → FEATURE_BIT_STATE_READING_0001  (model number, re-read)
    → FEATURE_BIT_STATE_READING_0002  (serial number, re-read)
      → FEATURE_BIT_STATE_READING_0092  (common feature API)
        → FEATURE_BIT_STATE_READING_0093  (appliance feature API 0)
          → ... (0094, 0095, 0096, 0097, 0109, 010A, 010B, 010C, 010D)
            → FEATURE_BIT_STATE_PARSING
                  → FEATURE_BIT_STATE_COMPLETE
                      → valid_list_ready_ = true

Any read failure → skip to next ERD in sequence
```

Parsing is incremental — one ERD per `loop()` call — to avoid triggering ESPHome's 30 ms loop watchdog (the full parse can take 1+ seconds).

## Dependencies

- `tiny_gea3_erd_client` — ERD client interface
- `appliance_api_feature_lists.h` — generated descriptor tables for feature bit parsing
- `geappliances_bridge_constants.h` — ERD constants

## Key Design Decisions

- **Deferred parsing**: The `parse_pending_` flag is set when all ERDs are read, but actual parsing happens incrementally in `run()` (one ERD per `loop()` call). This prevents blocking the ESPHome loop for too long.
- **Queue retry tolerance**: Each ERD read is retried up to 1000 times if the queue is full, then skipped. This handles busy bus conditions without stalling the entire sequence.
- **Mandatory ERDs**: The final valid ERD list always includes the feature bit ERDs themselves (0x0092–0x010D) plus identity ERDs (0x0001, 0x0002, 0x0008), regardless of feature bit values.
- **Common features first**: ERD 0x0092 (common features) is parsed first, then appliance-specific ERDs (0x0093–0x010D) are matched against descriptor tables by appliance type and version.

## Testing

Covered by integration tests in `test/tests/` through the full startup sequence. The parsing logic is tested against known feature bit patterns for various appliance types.
