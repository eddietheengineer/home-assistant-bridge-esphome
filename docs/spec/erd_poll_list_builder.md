# ERD Poll List Builder — Specification

## 1. Overview

### 1.1 Purpose

The ERD poll list builder is a **pure function** that constructs a deduplicated list of ERDs for the polling bridge to probe. Given the current operating mode, subscription state, feature-bit results, custom ERDs, and appliance type, it returns a fixed-capacity array of ERD identifiers — no HSM, no timers, no I/O.

### 1.2 Responsibilities

- Select the appropriate ERD set based on bridge mode and configuration
- Include appliance-specific ERDs when the appliance type is valid
- Deduplicate the resulting list in-place
- Return a human-readable description of how the list was constructed (for logging)

### 1.3 Not Responsible For

- Executing ERD reads or managing the probe phase (see `erd_bridge_poll`)
- Subscription-mode operation (see `erd_bridge_subscribe`)
- Any MQTT behavior
- Bridge startup phase management (see `geappliances_bridge_startup_hsm`)
- Feature bit parsing (see `feature_bit_manager`)

---

## 2. Public API

### 2.1 Function Signature

```cpp
ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config);
```

### 2.2 ErdPollListConfig

| Field | Type | Description |
|-------|------|-------------|
| `mode` | `BridgeMode` | Operating mode: `BRIDGE_MODE_POLL`, `BRIDGE_MODE_SUBSCRIBE`, or `BRIDGE_MODE_AUTO`. |
| `subscription_active` | `bool` | Whether subscription is currently active and confirmed. Relevant when mode is `SUBSCRIBE` or `AUTO`. |
| `appliance_api_parsing` | `bool` | Whether appliance API feature bit filtering is enabled. When `true`, only ERDs reported by the feature bits are included. |
| `feature_bit_valid_erds` | `const tiny_erd_t*` | Raw pointer into the feature bit manager's fixed array. `nullptr` if not available. |
| `feature_bit_valid_erds_count` | `uint16_t` | Number of valid ERDs in `feature_bit_valid_erds`. |
| `custom_erds` | `const uint16_t*` | Raw pointer to user-configured custom ERDs. `nullptr` if none. |
| `custom_erds_count` | `uint16_t` | Number of custom ERDs. |
| `appliance_type` | `uint8_t` | Discovered appliance type (0–255). Used to look up appliance-specific ERDs from `erd_lists.h`. |

### 2.3 ErdPollListResult

| Field | Type | Description |
|-------|------|-------------|
| `erds` | `uint16_t[ERD_POLL_LIST_MAX_SIZE]` | Fixed-capacity array of ERDs to probe. Capacity is 649, matching `POLLING_LIST_MAX_SIZE`. |
| `erds_count` | `uint16_t` | Number of valid ERDs in `erds`. |
| `description` | `const char*` | Human-readable string describing how the list was built (for logging). Never `nullptr`; initialized to empty string, then set to a descriptive string literal by the active code path. |

### 2.4 Return Semantics

The function returns a value (not a pointer). The `erds` array is embedded in the result struct. The caller is responsible for storing the result in a location whose lifetime covers the probe phase. The bridge stores the result's `erds` vector in `GeappliancesBridge::poll_probe_list_`.

---

## 3. Decision Logic

The function first determines whether the effective mode is subscription-based:

```
is_subscribe_mode = (mode == SUBSCRIBE) || (mode == AUTO && subscription_active)
```

If `is_subscribe_mode` is `true`, only custom ERDs are returned. Otherwise, the function falls through to poll-mode logic, which branches on `appliance_api_parsing`.

| Mode | Condition | Probe List Contents |
|------|-----------|---------------------|
| SUBSCRIBE | `subscription_active == true` | Custom ERDs only |
| SUBSCRIBE | `subscription_active == false` | Falls through to poll-mode logic (same as POLL) |
| POLL | `appliance_api_parsing == true` | Feature-bit valid ERDs + custom ERDs |
| POLL | `appliance_api_parsing == false` | `commonErds` + `energyErds` + `applianceApiFeatureErds` + appliance-specific ERDs + custom ERDs |
| AUTO | `subscription_active == true` | Custom ERDs only (same as SUBSCRIBE) |
| AUTO | `subscription_active == false` | Falls through to poll-mode logic (same as POLL) |

### 3.1 Description Strings

| Path | `description` value |
|------|---------------------|
| Subscription mode (custom only) | `"subscription mode: custom ERDs only"` |
| Poll mode with API parsing | `"poll mode with API parsing: feature-bit ERDs + custom ERDs"` |
| Poll mode without API parsing | `"poll mode without API parsing: full ERD list"` |

---

## 4. Deduplication

### 4.1 Algorithm

After all ERD groups are appended to the result array, the function performs in-place deduplication:

1. Sort the array in ascending order via `std::sort`
2. Compact duplicates by scanning with a read/write pointer pair
3. Update `erds_count` to the number of unique entries

### 4.2 Order

Before deduplication, ERDs are appended in this order: standard ERDs first (in their original group order from `erd_lists.h`), then custom ERDs last. After deduplication (which sorts the array), the result is in ascending ERD-identifier order. This means the original group ordering is not preserved after deduplication — the sort is the final ordering.

### 4.3 Capacity Guard

The helper `append_erds()` caps each append at the remaining capacity (`ERD_POLL_LIST_MAX_SIZE - current_count`). If a source list exceeds remaining capacity, only as many ERDs as will fit are copied. Deduplication runs after all appends, so the final count may be less than or equal to the pre-dedup count. Note that `append_erds()` operates on `uint16_t*` internally (not `tiny_erd_t*`), so `feature_bit_valid_erds` is implicitly cast from `const tiny_erd_t*` to `const uint16_t*` when passed — this relies on `tiny_erd_t` being layout-compatible with `uint16_t` for the ERD identifier field.

---

## 5. Null Safety

All pointer fields in `ErdPollListConfig` are checked for `nullptr` before dereference:

- `feature_bit_valid_erds`: The `append_erds()` helper checks `list != nullptr` before `std::memcpy`. A `nullptr` pointer with count > 0 is treated as an empty source.
- `custom_erds`: Same null check in `append_erds()`.

Zero counts (`feature_bit_valid_erds_count == 0` or `custom_erds_count == 0`) are handled correctly even with a non-null pointer — `append_erds()` copies zero bytes.

---

## 6. Appliance Type Bounds

When building the full ERD list (poll mode without API parsing), appliance-specific ERDs are looked up from `applianceTypeToErdGroupTranslation` using `config.appliance_type` as the index.

- If `config.appliance_type < maximumApplianceType`, the corresponding ERD group is appended
- If `config.appliance_type >= maximumApplianceType`, appliance-specific ERDs are skipped entirely

This prevents out-of-bounds access to the translation table. The constant `maximumApplianceType` is defined in `erd_lists.h` as the size of the translation array.

---

## 7. Invariants

1. **Pure function:** No mutable state, no side effects. Given identical input, the function always produces identical output.
2. **Deterministic output:** The result is fully determined by the config parameters and the static data in `erd_lists.h`. No randomness, no time dependence, no external I/O.
3. **Fixed-capacity result:** The `erds` array has a compile-time capacity of 649 (`ERD_POLL_LIST_MAX_SIZE`). No heap allocation occurs.
4. **Zero-initialized output:** The result array is zeroed via `std::memset` before any ERDs are appended.
5. **Non-null description:** The `description` field is never `nullptr`; it is initialized to an empty string and then set to a descriptive string literal by the active code path.
6. **No modification of input:** The function takes `config` by const reference and never modifies it or any data it points to.

---

## 8. Dependencies

| Dependency | Role |
|------------|------|
| `bridge_mode.h` | `BridgeMode` enum (`BRIDGE_MODE_POLL`, `BRIDGE_MODE_SUBSCRIBE`, `BRIDGE_MODE_AUTO`) |
| `erd_lists.h` | Static ERD lists: `commonErds`, `energyErds`, `applianceApiFeatureErds`, `applianceTypeToErdGroupTranslation`, `maximumApplianceType`, and their respective count constants |
| `tiny_erd.h` | `tiny_erd_t` type definition |
| `<cstdint>` | `uint8_t`, `uint16_t` |
| `<algorithm>` | `std::sort` |

---

## 9. Known Limitations

1. **Result array lifetime managed by caller:** The function returns a value with an embedded fixed array. The caller must store this result in a location whose lifetime covers the probe phase. The bridge stores the result in `GeappliancesBridge::poll_probe_list_`.
2. **Deduplication destroys original group ordering:** The sort-based deduplication produces ascending ERD-identifier order, not the original group order (common → energy → appliance API → appliance-specific → custom). This is acceptable because the probe phase reads ERDs sequentially regardless of their original grouping.
3. **No validation of custom ERD values:** Custom ERDs are appended as-is without checking whether they are valid ERD identifiers or whether they overlap with standard ERDs (overlap is handled by deduplication).
4. **Capacity overflow logs a warning:** If the total number of ERDs exceeds `ERD_POLL_LIST_MAX_SIZE`, excess ERDs are dropped at the point where capacity is reached. The function logs a warning via ESP_LOGW when ERDs are dropped.
