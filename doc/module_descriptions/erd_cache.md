# ERD Cache

## Purpose

Fixed-size ERD cache with inline/heap data storage. Stores the latest data for up to `ERD_CACHE_CAPACITY` (200) ERDs. ERDs ≤ 4 bytes are stored inline (zero heap); larger ERDs use heap allocation. ERD size is invariant after registration — updates are in-place `memcpy` with no alloc or free. Change detection is done at insert/update time, eliminating per-read `memcmp` overhead.

## Public API

| Function | Description |
|----------|-------------|
| `erd_cache_init(self)` | Initialize the cache (zero all entries, reset counters, free any heap data). |
| `erd_cache_destroy(self)` | Free any heap-allocated data, reset the cache. |
| `erd_cache_update(self, erd, data, data_size)` | Update or insert ERD data. Returns `true` if `update_required` was set (or entry was new). Returns `false` if cache is full, data is unchanged, or ERD size changed (appliance lost). |
| `erd_cache_set_throttle_rate_seconds(self, rate)` | Set minimum interval (seconds) between publishes per ERD. 0 = disabled. Range: 0–255. |
| `erd_cache_mark_published(self, entry)` | Mark an ERD entry as published; reloads the publish_cooldown timer. Static inline, zero overhead when rate limiting is disabled. |
| `erd_cache_tick_cooldowns(self)` | Decrement publish_cooldown for all entries with `update_required = true`. Call once per second. Static inline, no-op when rate limiting is disabled. |
| `erd_cache_get_next_updated(self, iterator)` | Returns the next entry with `update_required = true` and `publish_cooldown = 0`, then clears `update_required`. Skips entries still in cooldown, keeping `update_required = true` for retry. Caller provides an iterator (`uint16_t`) initialized to 0. Returns `NULL` when no more updated entries remain. |
| `erd_cache_get_count(self)` | Returns the number of valid entries currently in the cache. |
| `erd_cache_get_next_entry(self, iterator)` | Returns the next valid entry in the cache, iterating all entries. Does NOT require `update_required = true` and does NOT clear any flags — it is a read-only iteration. Resets iterator to 0 when exhausted. |
| `erd_cache_get_update_rate(self)` | Returns the number of cache updates since the last call, then resets the window counter. |
| `erd_cache_get_required_update_rate(self)` | Returns the number of updates that set `update_required = true` since the last call, then resets the window counter. |

## Storage Strategy

### Inline Storage (≤ 4 bytes)

ERDs with `data_size ≤ ERD_CACHE_INLINE_DATA_SIZE` (4 bytes) are stored directly in the `inline_data` union member. Zero heap allocation.

### Heap Storage (> 4 bytes)

ERDs with `data_size > 4` are allocated on the heap via `new uint8_t[data_size]`. The pointer is stored in `ext_data` and `uses_heap` is set to `true`. If heap allocation fails, data is truncated to 4 bytes inline.

### Storage Selection

Storage is determined on **registration only**. ERD data size is invariant after registration — the storage type is fixed for the lifetime of the entry:
- `data_size ≤ 4` → inline
- `data_size > 4` → heap

If a size change is detected on an existing entry, `erd_cache_update()` logs an error and returns `false`, signaling the appliance firmware has changed and the bridge should reinitialize.

## Update Flow

`erd_cache_update()` handles both new entries and updates to existing entries:

1. **Find existing entry** via `erd_cache_find()` (linear scan of `entries[]`)
2. **If found:**
   - Increment `update_count` and `update_count_window`
   - Check if data has changed via `erd_data_changed()` (memcmp only — size is invariant)
  - If data unchanged: return `false` (early exit, no storage churn)
   - If data size differs from existing: log error, return `false` (appliance lost)
   - In-place `memcpy` into existing buffer (inline or heap — no free or alloc)
  - Set `update_required = data_changed`
3. **If not found:**
   - Scan `entries[]` for the first slot with `valid == false`
   - If no free slot: return `false` (cache full)
   - Initialize entry, allocate storage (inline or heap, with truncation fallback)
   - Set `valid = true` and `update_required = true`

## Change Detection

`erd_data_changed()` compares the new data against the existing entry using `memcmp`. ERD size is invariant after registration, so only the data content is compared — no size check is needed.

## Entry Structure

```c
typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[ERD_CACHE_INLINE_DATA_SIZE];  // 4 bytes
    uint8_t* ext_data;  // heap pointer for data > 4 bytes
  };
  uint8_t data_size;
  bool uses_heap;       // true if ext_data is a heap allocation
  uint8_t publish_cooldown;  // counts down from max_cooldown to 0; 0 = eligible
  bool valid;
} erd_cache_entry_t;
```

## Cache Structure

```c
typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];  // 200 entries
  uint32_t update_count;              // total cache updates since init
  uint32_t update_count_window;       // updates since last get_update_rate() call
  uint32_t required_update_count;     // total updates setting update_required=true since init
  uint32_t required_update_count_window; // such updates since last get_required_update_rate() call
  uint8_t max_cooldown;              // configured rate limit in seconds; 0 = disabled
  bool initialized;                   // true after first successful erd_cache_init()
} erd_cache_t;
```

## Dependencies

- `tiny_gea3_erd_client.h` — `tiny_erd_t` type definition
- `<new>` — for heap allocation
- `<cstring>` — for `memcmp`
- `esphome/core/log.h` — for `ESP_LOGD`, `ESP_LOGW`, `ESP_LOGE`

## Key Design Decisions

- **Inline for small ERDs**: ERDs ≤ 4 bytes are stored inline in the entry struct — zero heap allocation, zero indirection. This covers the majority of ERDs (flags, device state, single-byte values).
- **Heap for larger ERDs**: ERDs > 4 bytes are allocated on the heap at registration. No pool is needed — with in-place updates there is no per-cycle alloc/free churn to avoid.
- **In-place updates**: Existing entries are updated with a single `memcpy` into the existing buffer — no free, no alloc. This eliminates all storage churn on every poll cycle.
- **Size invariance**: ERD data size never changes after registration. A size mismatch is treated as an appliance firmware change — `erd_cache_update()` logs an error and returns `false`, signaling the bridge should reinitialize.
- **Early exit on unchanged data**: When data hasn't changed, the update returns immediately without touching storage.
- **Change detection at update time**: `update_required` is set during `erd_cache_update()`, not during iteration. This eliminates per-read `memcmp` overhead in the publisher loop.
- **Two iterators**: `erd_cache_get_next_updated()` for the publisher (clears `update_required` flag) and `erd_cache_get_next_entry()` for read-only iteration.
- **Rate counters**: `update_count_window` and `required_update_count_window` accumulate updates and are reset by `get_update_rate()` and `get_required_update_rate()`. The window is determined by the call interval of the consumer (e.g. ~60s if called once per minute).
- **No eviction**: The cache has a fixed capacity with no eviction policy. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped. This is acceptable because the ERD set is bounded by the appliance's supported ERDs, which is typically well under 200.

## Testing

Covered indirectly through the unit tests for `erd_bridge_subscribe`, `erd_bridge_poll`, and `erd_cache_mqtt_publisher`. The cache is exercised through all bridge operations and publishing flows.
