/*!
 * @file
 * @brief Fixed-size ERD cache with hybrid inline/pool data storage.
 *
 * Stores the latest data for up to ERD_CACHE_CAPACITY ERDs.  ERDs <= 16 bytes
 * are stored inline (zero heap); larger ERDs use a fixed memory pool of
 * pre-allocated blocks to eliminate per-update heap fragmentation.
 * Change detection is done at insert/update time, eliminating per-read
 * memcmp overhead.
 */

#ifndef erd_cache_h
#define erd_cache_h

#include <stdint.h>
#include <stdbool.h>

#include "tiny_gea3_erd_client.h"

#define ERD_CACHE_INLINE_DATA_SIZE 16
#define ERD_CACHE_CAPACITY 200

/* Memory pool block sizes — covers the most common ERD data sizes.
 * Most ERDs are between 17 and 64 bytes.  The pool pre-allocates
 * blocks in these sizes to eliminate per-update new/delete churn.
 * ERDs larger than the largest pool block fall back to inline storage. */
#define ERD_CACHE_POOL_BLOCK_1  32
#define ERD_CACHE_POOL_BLOCK_2  48
#define ERD_CACHE_POOL_BLOCK_3  64
#define ERD_CACHE_POOL_BLOCK_4  128
#define ERD_CACHE_POOL_COUNT    4
typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[ERD_CACHE_INLINE_DATA_SIZE];
    uint8_t* pool_data;
  };
  uint8_t data_size;
  uint8_t pool_block_idx;  /* which pool block (0-3) or 255 if inline */
  bool uses_pool;
  bool update_required;
  bool valid;
} erd_cache_entry_t;

typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
  uint32_t update_count;              /* total cache updates since last window reset */
  uint32_t update_count_window;       /* total cache updates in the last 60s window */
  uint32_t required_update_count;     /* total updates setting update_required=true since reset */
  uint32_t required_update_count_window; /* updates setting update_required=true in last 60s */
  bool only_publish_onchange;         /* when true, only mark update_required on data change */

  /* Fixed memory pool — pre-allocated blocks to eliminate new/delete churn.
   * Each pool tier has ERD_CACHE_CAPACITY slots so every cache entry can
   * hold a block from any tier without contention. */
  uint8_t pool_blocks[ERD_CACHE_POOL_COUNT][ERD_CACHE_CAPACITY][ERD_CACHE_POOL_BLOCK_4];
  uint8_t pool_free[ERD_CACHE_POOL_COUNT][ERD_CACHE_CAPACITY];
} erd_cache_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_init(erd_cache_t* self);
void erd_cache_destroy(erd_cache_t* self);

/* Updates or inserts ERD data.
 * If only_publish_onchange is true: marks update_required only when data has changed.
 * If only_publish_onchange is false: always marks update_required=true.
 * New entries always mark update_required=true regardless of the setting.
 * Returns true if update_required was set (or entry was new).
 * Returns false if cache is full and the ERD is not already cached. */
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size);

/* Set whether the cache should only mark ERDs as updated when data changes.
 * Default is false (always mark updated). */
void erd_cache_set_only_publish_onchange(erd_cache_t* self, bool only_publish_onchange);

/* Returns the next entry with update_required=true, then clears the flag.
 * Caller provides an iterator (uint16_t) initialized to 0.
 * Returns NULL when no more updated entries remain.
 * NOTE: Declared for future use (e.g., batch republish after MQTT reconnect).
 *       Not used in the initial implementation. */
erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator);

/* Returns the number of valid entries currently in the cache. */
uint16_t erd_cache_get_count(erd_cache_t* self);
/* Returns the next valid entry in the cache, iterating all entries.
 * Caller provides an iterator (uint16_t) initialized to 0.
 * Returns NULL when no more valid entries remain (resets iterator to 0).
 * Unlike erd_cache_get_next_updated(), this does NOT require update_required=true
 * and does NOT clear any flags — it is a read-only iteration. */
erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator);

/* Returns the number of cache updates that occurred in the last 60 seconds,
 * then resets the window counter. */
uint32_t erd_cache_get_update_rate(erd_cache_t* self);

/* Returns the number of cache updates that set update_required=true in the last 60 seconds,
 * then resets the window counter. */
uint32_t erd_cache_get_required_update_rate(erd_cache_t* self);

#ifdef __cplusplus
}
#endif

#endif
