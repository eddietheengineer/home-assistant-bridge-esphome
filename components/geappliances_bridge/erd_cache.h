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
  uint8_t publish_cooldown;  /* counts down from max_cooldown to 0; 0 = eligible */
  bool valid;
} erd_cache_entry_t;

typedef struct erd_cache_t {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
  uint32_t update_count;              /* total cache updates since init */
  uint32_t update_count_window;       /* updates since last get_update_rate() call */
  uint32_t required_update_count;     /* total updates setting update_required=true since init */
  uint32_t required_update_count_window; /* such updates since last get_required_update_rate() call */
  uint8_t max_cooldown;              /* configured rate limit in seconds; 0 = disabled */
  bool initialized;                   /* true after first successful erd_cache_init() */
} erd_cache_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_init(erd_cache_t* self);
void erd_cache_destroy(erd_cache_t* self);

/* Updates or inserts ERD data.
 * Always marks update_required only when data has changed.
 * New entries always mark update_required=true.
 * Returns true if update_required was set (or entry was new).
 * Returns false if cache is full, data is unchanged,
 * or ERD size changed (appliance lost). */
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size);

/* Set the minimum interval (in seconds) between publishes for any ERD.
 * 0 = disabled (publish on every update). Range: 0-255. */
void erd_cache_set_throttle_rate_seconds(erd_cache_t* self, uint8_t rate);

/* Mark an ERD entry as successfully published to MQTT.
 * Reloads the publish_cooldown timer. Call after mqtt_client_publish_raw() succeeds.
 * Static inline — zero overhead when max_cooldown is 0 (early return).
 *
 * Thread safety: on ESP-IDF this is called from the background MQTT publisher
 * task while tick_cooldowns() runs from the main loop.  On single-core ESP32
 * uint8_t access is atomic and the tick→signal_work ordering in loop() ensures
 * the tick always runs before the task drains, so no additional locking is needed. */
static inline void erd_cache_mark_published(erd_cache_t* self, erd_cache_entry_t* entry) {
  if (self->max_cooldown == 0 || entry == NULL) return;
  entry->publish_cooldown = self->max_cooldown;
}

/* Decrement publish_cooldown for all entries with update_required=true.
 * Call once per second. Static inline — zero overhead when max_cooldown is 0.
 *
 * Thread safety: see erd_cache_mark_published() above.  This only touches
 * entries with update_required=true; mark_published() only touches entries
 * whose update_required was just cleared, so they operate on disjoint sets. */
static inline void erd_cache_tick_cooldowns(erd_cache_t* self) {
  if (self->max_cooldown == 0) return;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->update_required && e->publish_cooldown > 0) {
      e->publish_cooldown--;
    }
  }
}

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
