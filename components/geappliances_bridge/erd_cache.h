/*!
 * @file
 * @brief Fixed-size ERD cache with inline/heap data storage.
 *
 * Stores the latest data for up to ERD_CACHE_CAPACITY ERDs.  ERDs <= 4 bytes
 * are stored inline (zero heap); larger ERDs use heap allocation.  ERD size
 * is invariant after registration — updates are in-place memcpy with no alloc
 * or free.  Change detection is done at insert/update time, eliminating per-read
 * memcmp overhead.
 */

#ifndef erd_cache_h
#define erd_cache_h

#include <stdint.h>
#include <stdbool.h>

#include "tiny_gea3_erd_client.h"

#define ERD_CACHE_INLINE_DATA_SIZE 4
#define ERD_CACHE_CAPACITY 200

typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[ERD_CACHE_INLINE_DATA_SIZE];
    uint8_t* ext_data;  /* heap pointer for data > 4 bytes */
  };
  uint8_t data_size;
  bool uses_heap;       /* true if ext_data is a heap allocation */
  bool update_required;
  bool valid;
} erd_cache_entry_t;

typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
  uint32_t update_count;              /* total cache updates since init */
  uint32_t update_count_window;       /* updates since last get_update_rate() call */
  uint32_t required_update_count;     /* total updates setting update_required=true since init */
  uint32_t required_update_count_window; /* such updates since last get_required_update_rate() call */
  bool only_publish_onchange;         /* when true, only mark update_required on data change */
  bool initialized;                   /* true after first successful erd_cache_init() */
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
 * Returns false if cache is full, data is unchanged with only_publish_onchange,
 * or ERD size changed (appliance lost). */
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

/* Returns the number of cache updates since the last call, then resets the window counter. */
uint32_t erd_cache_get_update_rate(erd_cache_t* self);

/* Returns the number of updates that set update_required=true since the last call, then resets the window counter. */
uint32_t erd_cache_get_required_update_rate(erd_cache_t* self);

#ifdef __cplusplus
}
#endif

#endif
