# ErdRegistry

## Purpose

Be the single authoritative source for which ERDs are valid and which are registered at runtime. Owns the valid-ERD set (populated by FeatureBitManager at startup) and the registered-ERD set (appended by the MQTT adapter at runtime). Exposes query methods used by the MQTT adapter during publish and read-only accessors for diagnostics.

## Public API

### Setup Methods (called during bridge initialization)

| Method | Description |
|--------|-------------|
| `set_valid_erds(erds, count)` | Copy the valid-ERD set from FeatureBitManager and enable valid-ERD filtering. An empty set is ignored so filtering stays disabled. |
| `clear_registered_erds()` | Reset the registered-ERD set. Call before bridge (re-)initialization. |

### Runtime Methods (called by the MQTT adapter during operation)

| Method | Description |
|--------|-------------|
| `register_erd(erd)` | Record that an ERD has been registered at runtime. |

### Queries (used by MQTT adapter during register_erd)

| Method | Description |
|--------|-------------|
| `has_valid_erds_filter()` | Returns `true` if valid-ERD filtering is active. When `false`, all ERDs pass the filter. |
| `is_valid(erd)` | Returns `true` if the ERD passes the valid-ERD filter (or no filter active). |

### Read-Only Accessors
| Method | Description |
|--------|-------------|
| `registered_erd_count()` | Returns the number of registered ERDs. |
| `valid_erd_count()` | Returns the number of valid ERDs (filter). |
| `registered_erd(idx)` | Returns the registered ERD at the given index (0-based). |
| `valid_erd(idx)` | Returns the valid ERD at the given index (0-based). |

## Data Structures

```cpp
class ErdRegistry {
  tiny_erd_t valid_erds_[ERD_REGISTRY_MAX_VALID];    // 645 entries
  uint16_t valid_erds_count_{0};
  tiny_erd_t registered_erds_[ERD_REGISTRY_MAX_VALID];  // 645 entries
  uint16_t registered_erds_count_{0};
  bool valid_erds_ready_{false};
};
```

`ERD_REGISTRY_MAX_VALID` is 645. Note: `POLLING_LIST_MAX_SIZE` is 649 (defined in `erd_lists.h`); they do not match. The registry uses its own constant for the valid/registered ERD arrays, while the polling bridge uses `POLLING_LIST_MAX_SIZE` for its polling list and ERD set capacity.

## Usage Flow

1. **Startup**: `FeatureBitManager` reads and parses appliance feature bit ERDs, producing a list of valid ERDs.
2. **Bridge init**: `GeappliancesBridge` calls `erd_registry.set_valid_erds()` to populate the valid-ERD filter.
3. **Runtime**: `EsphomeMqttClientAdapter` calls `erd_registry.register_erd()` for each ERD it encounters. The adapter checks `is_valid()` before registering.
4. **Diagnostics**: The bridge reads `registered_erd_count()` and `valid_erd_count()` for logging.

## Dependencies

- `tiny_erd.h` — `tiny_erd_t` type definition

## Key Design Decisions

- **Fixed-capacity arrays**: Both `valid_erds_` and `registered_erds_` use fixed-capacity arrays (645 entries each) to avoid heap allocation. This matches the bounded nature of the ERD space.
- **Separate valid and registered sets**: The valid-ERD set is populated once at startup from the feature bit manager. The registered-ERD set grows at runtime as ERDs are encountered. This separation allows the MQTT adapter to filter by validity while tracking what has actually been seen.
- **Empty set disables filtering**: `set_valid_erds()` with an empty set is ignored — filtering stays disabled. This allows the bridge to operate without feature bit data (e.g., if feature bit reading fails or times out).
- **Deduplication on register**: `register_erd()` checks for duplicates before appending. If the ERD is already in the registered set, the call is a no-op. This prevents duplicate entries in the registered-ERD set.
- **No eviction**: Neither set evicts entries. The fixed capacity bounds memory usage.

## Testing

Exercised indirectly through the unit tests for `erd_bridge_poll` and `erd_cache_mqtt_publisher`. The registry is used by the MQTT adapter during ERD registration and by diagnostics for entity enumeration.
