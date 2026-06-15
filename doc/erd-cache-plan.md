# ERD Cache Implementation Plan

## Problem

Currently, ERD data is published to MQTT every time it's read (polling) or received (subscription), even when unchanged. The existing `erd_cache` in `mqtt_bridge_polling_t` is a `std::map<tiny_erd_t, vector<uint8_t>>` — it uses heap allocation per entry (std::map nodes + std::vector allocations), which fragments ESP32 heap over time. There is no cache in the subscription bridge at all.

The goal is to add a fixed-size array-based ERD cache that:
- Stores the latest data for each ERD
- Tracks whether each ERD's data has been updated since last publish
- Avoids per-entry heap allocation (std::map nodes, std::vector allocations)

## Architecture Analysis

### Current State

**Polling bridge** (`mqtt_bridge_polling_t`):
- `void* erd_cache` → `map<tiny_erd_t, vector<uint8_t>>` — used only for `only_publish_on_change` comparison in `state_polling::signal_read_completed` (lines 644-660)
- Cleared on entry to `state_polling` (line 564)
- Destroyed in `mqtt_bridge_polling_destroy` (line 853)

**Subscription bridge** (`mqtt_bridge_t`):
- No cache at all — every subscription publication triggers `mqtt_client_update_erd` (line 42-46 of mqtt_bridge.cpp)

### Key Data Points

| Field | Type | Notes |
|-------|------|-------|
| ERD number | `tiny_erd_t` (`uint16_t`) | 0x0000–0xFFFF |
| Data size | `uint8_t` | 0–255 bytes per ERD |
| Max ERDs in cache | 200 | Per requirement |

### Memory Budget (200 ERDs)

| Component | Size per entry | Total (200) |
|-----------|---------------|-------------|
| ERD number | 2 bytes | 400 B |
| Data pointer | 4 bytes | 800 B |
| Update required bool | 1 byte | 200 B |
| Data size | 1 byte | 200 B |
| **Entry struct** | **8 bytes** | **1,600 B** |
| **Data buffers** (200 × 256) | 256 bytes | **51,200 B** |
| **Total** | | **~52.8 KB** |

This is significant for ESP32 (~520KB total heap). We need to be strategic about data storage.

## Design

### Option A: Fixed-size inline data buffers (256 bytes each)

```c
typedef struct {
  tiny_erd_t erd;           // 2 bytes
  uint8_t data[256];        // 256 bytes — inline, max possible size
  uint8_t data_size;        // 1 byte
  bool update_required;     // 1 byte
  bool valid;               // 1 byte — entry is in use
} erd_cache_entry_t;

#define ERD_CACHE_CAPACITY 200
erd_cache_entry_t erd_cache[ERD_CACHE_CAPACITY];
```

**Total:** 200 × 261 = **52,200 bytes** (all static, no heap)

**Pros:** Zero heap allocation after init. Simple O(n) linear scan.
**Cons:** 52 KB even if most ERDs are small (most are 1-8 bytes). Wasteful.

### Option B: Heap-allocated data buffers per entry (chosen)

```c
typedef struct {
  tiny_erd_t erd;           // 2 bytes
  uint8_t* data;            // 4 bytes — heap pointer
  uint8_t data_size;        // 1 byte
  bool update_required;     // 1 byte
  bool valid;               // 1 byte — entry is in use
} erd_cache_entry_t;

#define ERD_CACHE_CAPACITY 200
erd_cache_entry_t erd_cache[ERD_CACHE_CAPACITY];
```

**Total:** 200 × 9 = **1,800 bytes** for the array + actual data sizes on heap (typically 200 × avg 8 bytes = ~1.6 KB)

**Pros:** Only allocates what's needed. Most ERDs are 1-8 bytes.
**Cons:** Still uses heap for data, but controlled allocation (one per ERD, not per read).

### Option C: Hybrid — small inline buffer + heap for large

```c
typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[16];
    uint8_t* heap_data;
  };
  uint8_t data_size;
  bool uses_heap;
  bool update_required;
  bool valid;
} erd_cache_entry_t;
```

**Pros:** Most ERDs (≤16 bytes) use no heap at all.
**Cons:** More complex code, slightly larger struct.

## Recommended Approach: Option B

**Rationale:**
- Most ERDs are 1-8 bytes (feature bits, status flags, simple values)
- The 1,800-byte fixed array is negligible
- Heap allocations are one per unique ERD, not per cycle — stable memory footprint
- Simpler than Option C, nearly as efficient for the data sizes we actually see
- Option A wastes ~50KB on ERDs that are typically 1-4 bytes

## Invariant: Only registered ERDs enter the cache

The cache only stores ERDs that have been **confirmed registered** — i.e., `mqtt_client_register_erd()` has been called and the ERD is in `erd_set`. This prevents:

1. **Failed discovery reads** — ERDs the appliance doesn't support (they go in `erd_set` to block retries, but have no valid data)
2. **Spurious reads** — concurrent bus traffic during `state_identify_appliance` that doesn't match the target ERD
3. **Pending-but-not-yet-registered ERDs** — ERDs in `pending_registration_set` are not cached until the first successful read triggers registration (line 637)

This means the cache write happens **after** the registration check in each handler, not before.

### 1. Create `erd_cache.h` / `erd_cache.cpp`

New module in `components/geappliances_bridge/`:

```c
// erd_cache.h
typedef struct {
  tiny_erd_t erd;
  uint8_t* data;
  uint8_t data_size;
  bool update_required;
  bool valid;
} erd_cache_entry_t;

#define ERD_CACHE_CAPACITY 200

typedef struct {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
} erd_cache_t;

void erd_cache_init(erd_cache_t* self);
void erd_cache_destroy(erd_cache_t* self);

// Returns pointer to entry, or NULL if not found
erd_cache_entry_t* erd_cache_find(erd_cache_t* self, tiny_erd_t erd);

// Updates or inserts ERD data. Returns true if data changed (for reads)
// or always true (for subscriptions).
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size, bool is_subscription);

// Returns the entry with update_required=true, then clears the flag.
// Caller must provide a way to iterate. Returns NULL when no more entries.
erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator);
```

**API contract:**
- `erd_cache_init(self)` — zeroes all entries; marks every entry as `valid = false`, `update_required = false`
- `erd_cache_destroy(self)` — frees each entry's `data` pointer if non-null; zeroes all fields
- `erd_cache_update(erd, data, size, true)` — subscription: always sets `update_required = true`; inserts new entry or updates existing one; returns `true`
- `erd_cache_update(erd, data, size, false)` — read: compares new data against cached; only sets `update_required = true` if data differs; returns `true` if data changed (or entry was new)
- `erd_cache_find(erd)` — returns entry pointer or NULL

**Note:** `erd_cache_get_next_updated()` is declared for future use (e.g., batch republish after MQTT reconnect). It is **not used** in the initial implementation.

### 2. Integrate into polling bridge

**In `mqtt_bridge_polling.h`:**
- Replace `void* erd_cache;` (line 87) with `erd_cache_t erd_cache;` (inline struct, no pointer indirection)

**In `mqtt_bridge_polling.cpp`:**

**a) Delete the `erd_cache()` helper function** (lines 54-57):
- The `static map<tiny_erd_t, vector<uint8_t>>& erd_cache(mqtt_bridge_polling_t* self)` helper is no longer needed — the field is now a direct struct, not a `void*` cast.

**b) `mqtt_bridge_polling_init_impl` (line 760):**
- Replace `self->erd_cache = reinterpret_cast<void*>(new map<tiny_erd_t, vector<uint8_t>>())` with `erd_cache_init(&self->erd_cache)`

**c) `mqtt_bridge_polling_destroy` (lines 853, 856):**
- Replace `delete reinterpret_cast<map<tiny_erd_t, vector<uint8_t>>*>(self->erd_cache)` with `erd_cache_destroy(&self->erd_cache)`
- Remove `self->erd_cache = nullptr` (no longer a pointer)

**d) `state_polling::entry` (line 564):**
- Remove `erd_cache(self).clear()` — the cache is not cleared here. Instead, the cache is reset at the start of discovery (see f below) and persists through the polling lifecycle.

**e) `state_polling::signal_read_completed` (lines 630-669):**
- Cache write happens **after** the registration block (lines 636-641). At this point the ERD is confirmed in `erd_set`.
- **Always** write to the cache regardless of `only_publish_on_change` — the cache is the source of truth for latest data:
  ```c
  bool data_changed = erd_cache_update(&self->erd_cache, erd, erd_data, data_size, false);
  ```
- The `should_publish` logic becomes:
  - If `only_publish_on_change` is true: `should_publish = data_changed`
  - If `only_publish_on_change` is false: `should_publish = true`
- This preserves the existing behavior exactly, but uses the cache's comparison instead of the map+vector approach.

**f) `handle_discovery_list_signals` (lines 253-259):**
- `add_erd_to_polling_list()` on line 254 calls `mqtt_client_register_erd()` — ERD is now confirmed.
- After `add_erd_to_polling_list()`, store in cache: `erd_cache_update(&self->erd_cache, erd, data, data_size, false)`
- Then call `mqtt_client_update_erd()` as before — the discovery phase always publishes (no `only_publish_on_change` check).

**g) `state_identify_appliance::signal_read_completed` (lines 352-373):**
- The 0x0008 appliance type read is **not registered** (it's not added to `erd_set` or the polling list). Do **not** cache it — it's discovery metadata, not a polled ERD.

**h) Polling bridge cache reset points:**
- The cache is reset in **three** places, each matching an `erd_set(self).clear()` call:
  1. `state_identify_appliance::entry` (line 332) — appliance lost re-entry with `api_parsed_list`
  2. `state_add_common_erds::entry` (line 409) — full discovery path start
  3. `state_add_appliance_api_feature_erds::entry` (line 451) — `api_parsed_list` path
- In each case, add `erd_cache_init(&self->erd_cache)` immediately after the `erd_set(self).clear()` line.
### 3. Integrate into subscription bridge

**In `mqtt_bridge.h`:**
- Add `erd_cache_t erd_cache;` field to `mqtt_bridge_t` struct (after `erd_set`)

**In `mqtt_bridge.cpp`:**

**a) `mqtt_bridge_init` (after line 164):**
- Add `erd_cache_init(&self->erd_cache)` after `erd_set` allocation

**b) `mqtt_bridge_destroy` (after line 235):**
- Add `erd_cache_destroy(&self->erd_cache)` before `delete erd_set`

**c) `sub_state_top::signal_subscription_publication_received` (lines 33-47):**
- Cache write happens **after** the registration block (lines 37-40). At this point the ERD is confirmed in `erd_set`.
- Replace the direct `mqtt_client_update_erd()` call (lines 42-46) with:
  ```c
  erd_cache_update(&self->erd_cache, erd,
    args->subscription_publication_received.data,
    args->subscription_publication_received.data_size,
    true);  // is_subscription = true
  mqtt_client_update_erd(self->mqtt_client, erd,
    args->subscription_publication_received.data,
    args->subscription_publication_received.data_size);
  ```
- Subscriptions always set `update_required = true` and always publish immediately. The cache serves as a record of latest values for potential future use (e.g., republish after MQTT reconnect).

**d) `state_subscribing::signal_subscription_host_came_online` (line 72):**
- Add `erd_cache_init(&self->erd_cache)` right after `erd_set(self).clear()` — matches the existing `erd_set` clear when the host restarts.

### 4. Publish flush mechanism

**Key question:** When do we actually publish the cached data?

**Current behavior:** Publish immediately on each read/subscription.
**New behavior:** The cache tracks updates, but we still publish immediately — the cache is used to:
1. Determine if data actually changed (for polling with `only_publish_on_change`)
2. Track what needs to be republished after MQTT reconnection

**Decision:** The cache replaces the existing `map<tiny_erd_t, vector<uint8_t>>` comparison logic. The publish decision remains immediate — we just use the cache's `update_required` flag instead of doing a fresh memcmp each cycle.

**For polling bridge:** `erd_cache_update(..., false)` returns true if data changed → publish immediately. This is the same behavior as the current `only_publish_on_change` logic, but without the O(n) map lookup + vector allocation per cycle.

**For subscription bridge:** `erd_cache_update(..., true)` always returns true → always publish. The cache serves as a record of latest values for potential future use (e.g., republish after MQTT reconnect).

### 5. Include and build system cleanup

**In `mqtt_bridge_polling.cpp`:**
- Remove `#include <map>` (line 26) — no longer needed
- Remove `#include <vector>` (line 28) — no longer needed
- Update the destroy comment (line 840) to reference `erd_cache` by name instead of `self->erd_cache`

**In `mqtt_bridge.cpp`:**
- Add `#include "erd_cache.h"`

**In `mqtt_bridge_polling.cpp`:**
- Add `#include "erd_cache.h"`

**In `Makefile` (line 23+):**
- Add `components/geappliances_bridge/erd_cache.cpp` to `SRC_FILES`

### 6. File structure

```
components/geappliances_bridge/
  ├── erd_cache.h          ← new: struct definitions + API
  ├── erd_cache.cpp        ← new: implementation
  ├── mqtt_bridge.h        ← modified: add erd_cache_t field
  ├── mqtt_bridge.cpp      ← modified: init/destroy/update cache, add #include
  ├── mqtt_bridge_polling.h ← modified: replace void* erd_cache with erd_cache_t
  └── mqtt_bridge_polling.cpp ← modified: use erd_cache API, remove map/vector includes
```

### 7. Tests

**`erd_cache_test.cpp`** in `test/tests/`:
- Test init/destroy
- Test insert first ERD
- Test update same ERD with same data (no change)
- Test update same ERD with different data (change detected)
- Test update same ERD with different size (change detected)
- Test subscription update (always marks as updated)
- Test capacity overflow (200+ ERDs — oldest not evicted, new ones rejected or last slot used)
- Test find non-existent ERD returns NULL
- Test iteration over updated entries
- Test destroy frees all heap data
### 8. Capacity overflow handling
When all 200 slots are full and a new ERD arrives:
- **Option:** Reject the new ERD (return false, don't cache it)
- **Rationale:** 200 is generous for any single appliance. If exceeded, the ERD still gets published via the normal path — it just won't be cached for change detection. This is a safe degradation.

### 9. Memory considerations
- **Fixed array:** 200 × 9 bytes = 1,800 bytes (stack or static)
- **Heap data:** 200 × avg 8 bytes = ~1,600 bytes (one allocation per ERD)
- **Total:** ~3.4 KB vs current map approach which allocates ~48 bytes per entry (map node) + vector overhead (~24 bytes) + data = ~80 bytes per entry for 200 ERDs = ~16 KB of heap fragmentation
- **Net savings:** ~12.6 KB less heap fragmentation, all in contiguous struct

## Execution Order

1. Create `erd_cache.h` and `erd_cache.cpp` with the core API
2. Write `erd_cache_test.cpp` and verify it passes
3. Modify `mqtt_bridge_polling.h` to use `erd_cache_t` inline
4. Modify `mqtt_bridge_polling.cpp` to use the new cache API
5. Modify `mqtt_bridge.h` to add `erd_cache_t` field
6. Modify `mqtt_bridge.cpp` to use the new cache API
7. Update `Makefile` or build system to include new files
8. Run existing tests to ensure no regressions
9. Update component tests if they reference the old cache
