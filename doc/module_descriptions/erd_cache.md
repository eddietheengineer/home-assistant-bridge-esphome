# ERD Cache

## Purpose

Fixed-size ERD cache with hybrid inline/pool data storage. Stores the latest data for up to `ERD_CACHE_CAPACITY` (200) ERDs. ERDs ≤ 4 bytes are stored inline (zero heap); larger ERDs use a fixed memory pool of pre-allocated blocks to eliminate per-update heap fragmentation. Change detection is done at insert/update time, eliminating per-read `memcmp` overhead.

## Public API

| Function | Description |
|----------|-------------|
| `erd_cache_init(self)` | Initialize the cache (zero all entries, reset counters, clear pool free lists). |
| `erd_cache_destroy(self)` | Free any heap-allocated data for entries that couldn't fit in the pool. |
| `erd_cache_update(self, erd, data, data_size)` | Update or insert ERD data. Returns `true` if `update_required` was set (or entry was new). Returns `false` if cache is full and the ERD is not already cached. |
| `erd_cache_set_only_publish_onchange(self, only_publish_onchange)` | Set whether the cache should only mark ERDs as updated when data changes. Default is `false` (always mark updated). |
| `erd_cache_get_next_updated(self, iterator)` | Returns the next entry with `update_required = true`, then clears the flag. Caller provides an iterator (`uint16_t`) initialized to 0. Returns `NULL` when no more updated entries remain. |
| `erd_cache_get_count(self)` | Returns the number of valid entries currently in the cache. |
| `erd_cache_get_next_entry(self, iterator)` | Returns the next valid entry in the cache, iterating all entries. Does NOT require `update_required = true` and does NOT clear any flags — it is a read-only iteration. Resets iterator to 0 when exhausted. |
| `erd_cache_get_update_rate(self)` | Returns the number of cache updates since the last call, then resets the window counter. |
| `erd_cache_get_required_update_rate(self)` | Returns the number of updates that set `update_required = true` since the last call, then resets the window counter. |

## Storage Strategy

### Inline Storage (≤ 4 bytes)

ERDs with `data_size ≤ ERD_CACHE_INLINE_DATA_SIZE` (4 bytes) are stored directly in the `inline_data` union member. Zero heap allocation, zero pool usage.

### Pool Storage (5–32 bytes)

ERDs with `data_size > 4` and `data_size ≤ 32` use the fixed memory pool. The pool has two tiers:

| Tier | Block Size | Count |
|------|-----------|-------|
| 0 | 16 bytes | `ERD_CACHE_CAPACITY` (200) |
| 1 | 32 bytes | `ERD_CACHE_CAPACITY` (200) |

Each tier has `ERD_CACHE_CAPACITY` slots so every cache entry can hold a block from any tier without contention. Blocks are tracked via a free-list (`pool_free[tier][slot]`).

### Heap Fallback (> 32 bytes)

ERDs with `data_size > 32` fall back to `new uint8_t[data_size]`. This is rare — most ERDs are between 5 and 32 bytes.

### Storage Selection

`pool_block_for_size()` determines the storage strategy:
- `data_size ≤ 4` → inline (returns 255)
- `5 ≤ data_size ≤ 16` → pool tier 0
- `17 ≤ data_size ≤ 32` → pool tier 1
- `data_size > 32` → heap (returns 255)

## Update Flow

`erd_cache_update()` handles both new entries and updates to existing entries:

1. **Find existing entry** via `erd_cache_find()` (linear scan of `entries[]`)
2. **If found:**
   - If `only_publish_onchange` is true: check if data has changed via `erd_data_changed()`
   - If data changed (or `only_publish_onchange` is false): free old storage, store new data, set `update_required = true`
   - If data unchanged and `only_publish_onchange` is true: do nothing
3. **If not found:**
   - Scan `entries[]` for the first slot with `valid == false`
   - If no free slot: return `false` (cache full)
   - Store data, set `valid = true` and `update_required = true`

## Change Detection

`erd_data_changed()` compares the new data against the existing entry:
- First checks `data_size` — if sizes differ, data has changed (fast path)
- If sizes are equal, does a `memcmp` of the shared length
- Avoids partial `memcmp` of mismatched lengths

## Entry Structure

```c
typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[ERD_CACHE_INLINE_DATA_SIZE];  // 4 bytes
    uint8_t* ext_data;  // pool or heap pointer
  };
  uint8_t data_size;
  uint8_t ext_alloc_size;  // allocated size for pool/heap buffer
  uint8_t pool_block_idx;  // which pool block (0-1) or 255 if not pool
  bool uses_pool;
  bool uses_heap;
  bool update_required;
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
  bool only_publish_onchange;         // when true, only mark update_required on data change
  bool initialized;                   // true after first successful erd_cache_init()
  uint8_t pool_blocks[ERD_CACHE_POOL_COUNT][ERD_CACHE_CAPACITY][ERD_CACHE_POOL_BLOCK_2];
  uint8_t pool_free[ERD_CACHE_POOL_COUNT][ERD_CACHE_CAPACITY];
} erd_cache_t;
```

## Dependencies

- `tiny_gea3_erd_client.h` — `tiny_erd_t` type definition
- `<new>` — for heap allocation fallback
- `<string.h>` — for `memcmp`, `memset`

## Key Design Decisions

- **Hybrid storage**: Inline for small ERDs (≤ 4 bytes), pool for medium (5–32 bytes), heap fallback for large (> 32 bytes). This eliminates per-update `new`/`delete` churn for the vast majority of ERDs.
- **Fixed pool capacity**: Each pool tier has `ERD_CACHE_CAPACITY` slots (200), so every cache entry can hold a block from any tier without contention. No dynamic resizing needed.
- **Change detection at update time**: `update_required` is set during `erd_cache_update()`, not during iteration. This eliminates per-read `memcmp` overhead in the publisher loop.
- **Two iterators**: `erd_cache_get_next_updated()` for the publisher (clears `update_required` flag) and `erd_cache_get_next_entry()` for read-only iteration (used by HA discovery).
- **Rate counters**: `update_count_window` and `required_update_count_window` accumulate updates and are reset by `get_update_rate()` and `get_required_update_rate()`. The window is determined by the call interval of the consumer (e.g. ~60s if called once per minute).
- **No eviction**: The cache has a fixed capacity with no eviction policy. If the cache is full and a new ERD arrives that isn't already cached, the update is silently dropped. This is acceptable because the ERD set is bounded by the appliance's supported ERDs, which is typically well under 200.

## Testing

Covered indirectly through the unit tests for `erd_bridge_subscribe`, `erd_bridge_poll`, and `erd_cache_mqtt_publisher`. The cache is exercised through all bridge operations and publishing flows.
