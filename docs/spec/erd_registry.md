# ERD Registry — Specification

## 1. Overview

### 1.1 Purpose

The ERD registry is the single authoritative source for which ERDs are valid (supported by the appliance) and which have been registered at runtime. It is populated at startup by the Feature Bit Manager and updated at runtime by the MQTT adapter as ERDs are encountered on the bus.

### 1.2 Responsibilities

- Own the valid-ERD set, populated once at startup from the Feature Bit Manager
- Own the registered-ERD set, appended to at runtime by the MQTT adapter
- Expose query methods used by the MQTT adapter to filter publish decisions
- Expose read-only accessors used by the Home Assistant discovery manager and diagnostics

### 1.3 Not Responsible For

- Reading ERDs from the appliance (Feature Bit Manager does that)
- Publishing MQTT messages (EsphomeMqttClientAdapter does that)
- Bridge lifecycle or startup management
- ERD value caching or polling

---

## 2. Setup Methods

### 2.1 Set Valid ERDs

```cpp
void set_valid_erds(const tiny_erd_t* erds, uint16_t count);

```

Copies the valid-ERD set from the Feature Bit Manager and enables valid-ERD filtering.

| Parameter | Description |
|-----------|-------------|
| `erds` | Pointer to the array of valid ERDs from the Feature Bit Manager. |
| `count` | Number of valid ERDs in the array. |

**Behavior:**
- If `erds` is null or `count` is zero: returns immediately; the current filter state is unchanged (if filtering was previously enabled, it stays enabled). This prevents an empty set from silently suppressing all publishes.
- If `count` exceeds `ERD_REGISTRY_MAX_VALID`: copies only the first `ERD_REGISTRY_MAX_VALID` entries.
- On success: sorts `valid_erds_` in ascending order (for binary search), then sets `valid_erds_ready_` to `true`, activating the filter.

Called once during bridge initialization, after the Feature Bit Manager completes parsing.

### 2.2 Clear Registered ERDs

```cpp
void clear_registered_erds();
```

Resets the registered-ERD count to zero. Called before bridge (re-)initialization to ensure a clean state. Does not modify the valid-ERD set.

---

## 3. Runtime Methods

### 3.1 Register ERD

```cpp
void register_erd(tiny_erd_t erd);
```

Records that an ERD has been seen at runtime. Called by the MQTT adapter each time an ERD is encountered on the bus.

**Behavior:**
- **Deduplication:** Scans the existing registered set; if the ERD is already present, returns immediately.
- **Capacity guard:** If `registered_erds_count_ >= ERD_REGISTRY_MAX_VALID`, drops the ERD.
- Otherwise: appends to `registered_erds_` and increments `registered_erds_count_`.

---

## 4. Queries

### 4.1 Has Valid ERDs Filter

```cpp
bool has_valid_erds_filter() const;
```

Returns `valid_erds_ready_`. When `false`, no filter is active and all ERDs pass validation. When `true`, only ERDs present in the valid set are accepted.

### 4.2 Is Valid

```cpp
bool is_valid(tiny_erd_t erd) const;
```

Returns `true` if the ERD passes the valid-ERD filter.

**Behavior:**
- If `valid_erds_ready_` is `false`: returns `true` (no filter active — all ERDs are valid).
- Otherwise: performs a binary search on `valid_erds_`; returns `true` if found, `false` otherwise.

---

## 5. Read-Only Accessors

### 5.1 Counts

```cpp
uint16_t registered_erd_count() const;
uint16_t valid_erd_count() const;
```

Return the current number of registered ERDs and valid ERDs, respectively.

### 5.2 Indexed Access

```cpp
tiny_erd_t registered_erd(uint16_t idx) const;
tiny_erd_t valid_erd(uint16_t idx) const;
```

Return the ERD at the given 0-based index. If `idx` is out of range, return `0`.

---

## 6. Data Structures

```cpp
#define ERD_REGISTRY_MAX_VALID 645

class ErdRegistry {
    tiny_erd_t valid_erds_[ERD_REGISTRY_MAX_VALID];
    uint16_t valid_erds_count_{0};
    tiny_erd_t registered_erds_[ERD_REGISTRY_MAX_VALID];
    uint16_t registered_erds_count_{0};
    bool valid_erds_ready_{false};
};
```

| Field | Type | Description |
|-------|------|-------------|
| `valid_erds_` | `tiny_erd_t[645]` | Fixed-capacity array of valid ERDs. Populated once at startup. |
| `valid_erds_count_` | `uint16_t` | Number of valid ERDs in the array. |
| `registered_erds_` | `tiny_erd_t[645]` | Fixed-capacity array of ERDs registered at runtime. |
| `registered_erds_count_` | `uint16_t` | Number of registered ERDs in the array. |
| `valid_erds_ready_` | `bool` | Gate: when `true`, the valid-ERD filter is active. |

Both arrays share the same capacity constant (`ERD_REGISTRY_MAX_VALID = 645`). The valid set is write-once (set at startup, never modified). The registered set is append-only (with dedup).

---

## 7. Usage Flow

```
Startup
  │
  ├─ Feature Bit Manager reads and parses feature bit ERDs
  │
  └─ Bridge init calls set_valid_erds(valid_erds, count)
       │
       ├─ If count > 0: valid_erds_ready_ = true (filter active)
       └─ If count == 0: valid_erds_ready_ = false (no filter)

Bridge (Re-)Initialization
  │
  └─ clear_registered_erds() — resets registered set

Runtime
  │
  └─ MQTT adapter calls register_erd(erd) for each ERD seen on the bus
       │
       ├─ Dedup check → skip if already registered
       └─ Capacity check → skip if array full

Diagnostics
  │
  ├─ valid_erd_count() / valid_erd(idx) — enumerate valid ERDs
  ├─ registered_erd_count() / registered_erd(idx) — enumerate registered ERDs
  └─ is_valid(erd) — filter publish decisions
```

---

## 8. Invariants

1. **Fixed-capacity arrays:** Both `valid_erds_` and `registered_erds_` are bounded by `ERD_REGISTRY_MAX_VALID` (645). No dynamic allocation.
2. **Separate valid and registered sets:** The valid set defines what the appliance supports; the registered set tracks what has been seen at runtime. They are independent — an ERD can be valid but not yet registered, or registered but not valid.
3. **Empty valid set preserves filter state:** If `set_valid_erds()` receives a null pointer or zero count, it returns immediately without modifying `valid_erds_ready_`. If filtering was previously disabled, it stays disabled and `is_valid()` returns `true` for all ERDs. If filtering was previously enabled, it stays enabled.
4. **Dedup on register:** `register_erd()` checks for duplicates before appending. The same ERD is never registered twice.
5. **No eviction:** Once an ERD is registered or marked valid, it remains in its respective set for the lifetime of the registry. There is no removal or rotation.
6. **Valid set is write-once:** After `set_valid_erds()` is called, the valid set is never modified.
7. **Out-of-bounds accessors return zero:** `registered_erd(idx)` and `valid_erd(idx)` return `0` when `idx` exceeds the current count.

---

## 9. Dependencies

| Dependency | Role |
|------------|------|
| `tiny_erd.h` | Defines `tiny_erd_t` type used for all ERD values. |

---

## 10. Known Limitations

1. **Fixed capacity:** Both arrays are capped at 645 entries. If the appliance reports more than 645 valid ERDs, excess entries are silently dropped. If more than 645 unique ERDs are registered at runtime, excess registrations are dropped.
2. **Binary search for `is_valid()`:** Validation uses `std::binary_search` on the sorted valid set. O(log n) lookup, bounded by the 645-entry capacity.
3. **No removal:** ERDs cannot be unregistered or removed from the valid set. If the appliance's supported ERD set changes at runtime (e.g., firmware update), the registry must be re-initialized.
4. **No ordering guarantee:** The valid set is sorted in ascending order after `set_valid_erds()` is called, to enable binary search in `is_valid()`. The registered set is ordered by first registration time. Neither preserves the original input order.
