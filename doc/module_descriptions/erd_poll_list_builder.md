# ErdPollListBuilder

## Purpose

Builds the list of ERDs to probe for the polling bridge based on the current operating mode, subscription state, feature-bit results, custom ERDs, and appliance type. This is a **pure function** — no HSM, no timers, no I/O.

## Public API

```cpp
ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config);
```

| Parameter | Description |
|-----------|-------------|
| `config.mode` | The operating mode: `BRIDGE_MODE_POLL`, `BRIDGE_MODE_SUBSCRIBE`, or `BRIDGE_MODE_AUTO`. |
| `config.subscription_capable` | Whether the appliance supports GEA3 subscriptions (always false for GEA2). |
| `config.subscription_active` | Whether subscription is currently active and confirmed. Only relevant when mode is SUBSCRIBE or AUTO. |
| `config.appliance_api_parsing` | Whether appliance API feature bit filtering is enabled. When true, only ERDs reported by the feature bits are included. |
| `config.feature_bit_valid_erds` | The valid ERD set produced by the feature bit manager. Empty if feature bits are not available or parsing is disabled. |
| `config.custom_erds` | User-configured custom ERDs to always poll. |
| `config.appliance_type` | The discovered appliance type (0-255). Used to look up appliance-specific ERDs from `erd_lists.h`. |

**Returns:** An `ErdPollListResult` containing:
- `erds`: A deduplicated `std::vector<uint16_t>` of ERDs to probe.
- `description`: A human-readable string describing how the list was built (for logging).

## Decision Logic

| Mode | Condition | Probe List Contents |
|------|-----------|---------------------|
| SUBSCRIBE | subscription confirmed | custom ERDs only |
| POLL | appliance_api_parsing = true | feature-bit valid ERDs + custom ERDs |
| POLL | appliance_api_parsing = false | common + energy + appliance API feature + appliance-specific + custom ERDs |
| AUTO | subscription active | custom ERDs only |
| AUTO | subscription not active (fallback) | same as POLL with current appliance_api_parsing |

The returned list is deduplicated. Order is: standard ERDs first (in their original group order), then custom ERDs.

## Usage

Called by `build_poll_list_()` in `geappliances_bridge_bridge_init.cpp` during:
- **Polling mode initialization** (`initialize_erd_bridge_()`): builds the full probe list before `erd_bridge_poll_init()`.
- **Custom ERD polling** (`start_custom_erd_polling_()`): builds a custom-only probe list after the subscription quiet window elapses.
- **AUTO mode fallback** (`handle_subscription_failed()`): builds the poll-mode probe list after subscription enters the failed state.

The result's `erds` vector is stored in `GeappliancesBridge::poll_probe_list_` so the pointer remains valid across the probe phase.

## Dependencies

- `bridge_mode.h` — `BridgeMode` enum
- `erd_lists.h` — static ERD lists (`commonErds`, `energyErds`, `applianceApiFeatureErds`, `applianceTypeToErdGroupTranslation`, `maximumApplianceType`)
- `<set>`, `<vector>`, `<string>`, `<cstdint>` — standard library

## Key Design Decisions

- **Pure function**: No state, no side effects. Given the same input, always produces the same output. This makes it trivially testable and easy to reason about.
- **Deduplication**: The result is deduplicated in-place using a `std::set`. This handles cases where custom ERDs overlap with standard ERDs or feature-bit ERDs.
- **Ordering**: Standard ERDs come first (in their original group order), then custom ERDs. This ensures that standard ERDs are probed before custom ones, matching the previous multi-phase discovery order.
- **Null safety**: All pointer fields (`feature_bit_valid_erds`, `custom_erds`) are checked for null before dereferencing. Empty vectors are treated as if the field were null.
- **Appliance type bounds check**: If `appliance_type >= maximumApplianceType`, appliance-specific ERDs are skipped (same behavior as the previous multi-phase discovery).

## Testing

Tested indirectly through the unit tests in `test/tests/erd_bridge_poll_test.cpp` (probe list tests) and `test/tests/erd_bridge_poll_test.cpp` (broadcast and empty list tests). The builder itself is not tested in isolation — it is exercised through the full bridge initialization flow.
