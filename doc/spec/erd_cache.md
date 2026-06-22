# ERD Cache — Specification

## 1. Overview

### 1.1 Purpose

The ERD cache is a fixed-size store for the latest data of up to `ERD_CACHE_CAPACITY` (200) ERDs. It provides inline storage for ERDs ≤ 4 bytes (zero heap allocation) and heap storage for larger ERDs. ERD data size is invariant after registration — updates are in-place `memcpy` with no allocation or deallocation. Change detection is performed at insert/update time, eliminating per-read `memcmp` overhead in the publisher loop.

### 1.2 Responsibilities

- Store the latest data for each ERD observed by the bridge
- Choose inline or heap storage at registration based on data size
- Detect data changes via `memcmp` and set `update_required` accordingly
- Provide iteration over updated entries (for publishing) and all entries
- Track update rates via windowed counters

### 1.3 Not Responsible For

- ERD discovery or polling
- Publishing data to MQTT
- Home Assistant discovery payload generation
- Managing the lifecycle of the bridge or ERD client

---

## 2. Initialization

### 2.1 Init

```c
void erd_cache_init(erd_cache_t* self);
```

Initializes the cache for use. Safe to call on a previously-initialized cache (re-init).

**Behavior:**
1. Checks `self->initialized` and saves it as `was_initialized`, then sets `self->initialized = false`
2. If `was_initialized` is true: iterates all entries and frees any heap-allocated `ext_data` via `delete[]`
3. Explicitly zeroes all entry fields (erd, data_size, uses_heap, update_required, valid, ext_data) — avoids UBSan issues with bool fields after `memset`
4. Resets all counters (`update_count`, `update_count_window`, `required_update_count`, `required_update_count_window`) to zero
5. Sets `self->initialized = true`

The guard on `was_initialized` makes the function safe against callers that pass a non-zeroed struct (stack garbage) — the free loop is skipped in that case.

### 2.2 Destroy

```c
void erd_cache_destroy(erd_cache_t* self);
```

Frees all heap-allocated data and resets the cache. Guards against being called on a never-initialized struct (checks `self->initialized`).

**Behavior:**
1. If `!self->initialized`, returns immediately
2. Calls `erd_cache_init(self)` to free heap data and zero all entries
3. Sets `self->initialized = false`

---

## 3. Storage Strategy

### 3.1 Inline Storage (≤ 4 bytes)

ERDs with `data_size ≤ ERD_CACHE_INLINE_DATA_SIZE` (4 bytes) are stored directly in the `inline_data` union member. Zero heap allocation, zero indirection. This covers the majority of ERDs (flags, device state, single-byte values).

### 3.2 Heap Storage (> 4 bytes)

ERDs with `data_size > 4` are allocated on the heap via `new (std::nothrow) uint8_t[data_size]`. The pointer is stored in `ext_data` and `uses_heap` is set to `true`.

If heap allocation fails, data is truncated to 4 bytes inline: `memcpy` copies `ERD_CACHE_INLINE_DATA_SIZE` bytes into `inline_data`, `data_size` is set to 4, and `uses_heap` remains `false`. A one-time warning is logged.

### 3.3 Size Invariance

Storage tier is determined on **registration only**. ERD data size is invariant after registration — the storage type is fixed for the lifetime of the entry. If a size change is detected on an existing entry, `erd_cache_update()` logs an error and returns `false`, signaling the appliance firmware has changed and the bridge should reinitialize.

---

## 4. Update Flow

`erd_cache_update()` handles both new entries and updates to existing entries:

```c
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size);
```

### 4.1 Existing Entry

1. Find existing entry via `erd_cache_find()` (linear scan of `entries[]`)
2. Increment `update_count` and `update_count_window`
3. **Size check:** If `data_size != existing->data_size`: log error, return `false` (appliance lost)
4. **Change detection:** Call `erd_data_changed()` — `memcmp` of new data against existing buffer using `existing->data_size`
5. **Early exit:** If `!data_changed`: return `false` (no storage churn)
6. **In-place update:** `memcpy` into existing buffer (`ext_data` if heap, `inline_data` if inline)
7. **Set flag:** `existing->update_required = data_changed`
8. If `update_required` is true, increment `required_update_count` and `required_update_count_window`
9. Return `existing->update_required`

### 4.2 New Entry

1. Scan `entries[]` for the first slot with `valid == false`
2. If no free slot: if not already warned, log a one-time warning and set `s_overflow_warned = true`; return `false`
3. Increment `update_count`, `update_count_window`, `required_update_count`, `required_update_count_window`
4. Initialize entry: set `erd`, `valid = true`, `update_required = true`, `uses_heap = false`, `ext_data = NULL`, `data_size = 0`
5. **Storage selection:**
   - If `data_size ≤ ERD_CACHE_INLINE_DATA_SIZE`: `memcpy` into `inline_data`, set `data_size`
   - If `data_size > ERD_CACHE_INLINE_DATA_SIZE`: allocate heap via `new (std::nothrow)`, copy data, set `uses_heap = true` and `data_size`; on allocation failure, truncate to 4 bytes inline
6. Return `true`

---

## 5. Change Detection

`erd_data_changed()` compares the new data against the existing entry using `memcmp`:

```c
static bool erd_data_changed(const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size);
```

- The `new_size` parameter is unused (size is invariant); `existing->data_size` is used for the `memcmp` length to guard against OOB reads
- Reads from `existing->ext_data` if `uses_heap`, otherwise `existing->inline_data`
- Returns `true` if the data differs, `false` if identical

---

## 6. Data Structures

### 6.1 Entry

```c
typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[ERD_CACHE_INLINE_DATA_SIZE];  // 4 bytes
    uint8_t* ext_data;  // heap pointer for data > 4 bytes
  };
  uint8_t data_size;
  bool uses_heap;       // true if ext_data is a heap allocation
  bool update_required;
  bool valid;
} erd_cache_entry_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `erd` | `tiny_erd_t` | ERD identifier |
| `inline_data` | `uint8_t[4]` | Inline storage for small ERDs |
| `ext_data` | `uint8_t*` | Heap pointer for large ERDs |
| `data_size` | `uint8_t` | Actual data size (invariant after registration) |
| `uses_heap` | `bool` | True if data is in `ext_data` |
| `update_required` | `bool` | Set during update; cleared by `get_next_updated()` |
| `valid` | `bool` | True if entry is in use |

### 6.2 Cache

```c
typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];  // 200 entries
  uint32_t update_count;              // total cache updates since init
  uint32_t update_count_window;       // updates since last get_update_rate() call
  uint32_t required_update_count;     // total updates setting update_required=true since init
  uint32_t required_update_count_window; // such updates since last get_required_update_rate() call
  bool initialized;                   // true after first successful erd_cache_init()
} erd_cache_t;
```

| Field | Type | Description |
|-------|------|-------------|
| `entries` | `erd_cache_entry_t[200]` | Fixed array of cache entries |
| `update_count` | `uint32_t` | Total updates since init (cumulative) |
| `update_count_window` | `uint32_t` | Updates since last `get_update_rate()` call |
| `required_update_count` | `uint32_t` | Total updates that set `update_required=true` since init |
| `required_update_count_window` | `uint32_t` | Such updates since last `get_required_update_rate()` call |
| `initialized` | `bool` | Guard for safe init/destroy |

---

## 7. API

### 7.1 Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_init` | `void erd_cache_init(erd_cache_t* self)` | Initialize the cache. Frees any existing heap data, zeroes all entries, resets counters. Safe for re-init. |
| `erd_cache_destroy` | `void erd_cache_destroy(erd_cache_t* self)` | Free heap data, zero entries, set `initialized = false`. Guards against never-initialized structs. |

### 7.2 Update

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_update` | `bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size)` | Update or insert ERD data. Returns `true` if `update_required` was set (or entry was new). Returns `false` if cache is full, data is unchanged, or ERD size changed. |

### 7.3 Iteration

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_get_next_updated` | `erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator)` | Returns the next entry with `update_required = true`, then clears the flag. Caller provides an iterator (`uint16_t`) initialized to 0. Returns `NULL` when no more updated entries remain; resets iterator to 0. |
| `erd_cache_get_count` | `uint16_t erd_cache_get_count(erd_cache_t* self)` | Returns the number of valid entries currently in the cache. |
| `erd_cache_get_next_entry` | `erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator)` | Returns the next valid entry in the cache, iterating all entries. Does NOT require `update_required = true` and does NOT clear any flags — it is a read-only iteration. Resets iterator to 0 when exhausted. |

### 7.4 Rate Counters

| Function | Signature | Description |
|----------|-----------|-------------|
| `erd_cache_get_update_rate` | `uint32_t erd_cache_get_update_rate(erd_cache_t* self)` | Returns the number of cache updates since the last call, then resets `update_count_window` to zero. |
| `erd_cache_get_required_update_rate` | `uint32_t erd_cache_get_required_update_rate(erd_cache_t* self)` | Returns the number of updates that set `update_required = true` since the last call, then resets `required_update_count_window` to zero. |

The window is determined by the call interval of the consumer (e.g., ~60 s if called once per minute).

---

## 8. Invariants

1. **Size invariance:** ERD data size never changes after registration. A size mismatch on update is treated as an appliance firmware change — `erd_cache_update()` logs an error and returns `false`.

2. **No eviction:** The cache has a fixed capacity with no eviction policy. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped (with a one-time warning).

3. **No per-update allocation:** Updates to existing entries use in-place `memcpy` — no `new` or `delete` on the update path.

4. **Early exit on unchanged data:** When data hasn't changed, the update returns immediately without touching storage.

5. **Change detection at update time:** `update_required` is set during `erd_cache_update()`, not during iteration. This eliminates per-read `memcmp` overhead in the publisher loop.

6. **New entries always marked updated:** New entries always set `update_required = true`.

7. **Iterator reset on exhaustion:** Both `get_next_updated()` and `get_next_entry()` reset the iterator to 0 when no more entries are found, allowing the caller to restart iteration.

8. **Safe init/destroy:** `erd_cache_init()` guards against non-zeroed structs; `erd_cache_destroy()` guards against never-initialized structs.

---

## 9. Dependencies

| Dependency | Purpose |
|------------|---------|
| `tiny_gea3_erd_client.h` | `tiny_erd_t` type definition |
| `<new>` | `new (std::nothrow)` for heap allocation |
| `<cstring>` | `memcmp` for change detection, `memcpy` for data copy |
| `esphome/core/log.h` | `ESP_LOGD`, `ESP_LOGW`, `ESP_LOGE` for logging |

---

## 10. Known Limitations

1. **Fixed capacity with no eviction:** The cache holds at most 200 entries. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped. This is acceptable because the ERD set is bounded by the appliance's supported ERDs, which is typically well under 200.

2. **Heap allocation failure truncates to 4 bytes:** If `new` fails for a large ERD, data is truncated to the first 4 bytes stored inline. Subsequent updates to that ERD will use inline storage (since `data_size` is now 4), and any size mismatch from the original data will cause updates to fail with a size-change error.

3. **Linear scan for lookup:** `erd_cache_find()` scans entries sequentially. With 200 entries this is bounded and fast, but a hash map would be O(1).

4. **Linear scan for free slot:** New entry insertion scans for the first invalid slot, which is O(n) in the worst case.

5. **No batch republish:** `get_next_updated()` clears `update_required` per entry. If publishing fails mid-iteration (e.g., MQTT disconnect), the cleared entries are not re-marked. A batch republish mechanism would need to iterate all entries via `get_next_entry()` and re-publish.
