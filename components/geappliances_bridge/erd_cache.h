/*!
 * @file
 * @brief Fixed-size ERD cache with hybrid inline/heap data storage.
 *
 * Stores the latest data for up to ERD_CACHE_CAPACITY ERDs.  ERDs <= 16 bytes
 * are stored inline (zero heap); larger ERDs allocate a single heap buffer.
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

typedef struct {
  tiny_erd_t erd;
  union {
    uint8_t inline_data[ERD_CACHE_INLINE_DATA_SIZE];
    uint8_t* heap_data;
  };
  uint8_t data_size;
  bool uses_heap;
  bool update_required;
  bool valid;
} erd_cache_entry_t;

typedef struct {
  erd_cache_entry_t entries[ERD_CACHE_CAPACITY];
  uint32_t update_count;        // total updates since last window reset
  uint32_t update_count_window; // updates in the last 60s window
} erd_cache_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_init(erd_cache_t* self);
void erd_cache_destroy(erd_cache_t* self);

// Returns pointer to entry, or NULL if not found.
erd_cache_entry_t* erd_cache_find(erd_cache_t* self, tiny_erd_t erd);

// Updates or inserts ERD data.
// For reads (is_subscription=false): compares new data against cached;
//   returns true if data changed (or entry was new).
// For subscriptions (is_subscription=true): always sets update_required=true;
//   returns true.
// Returns false if cache is full and the ERD is not already cached.
bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size, bool is_subscription);

// Returns the next entry with update_required=true, then clears the flag.
// Caller provides an iterator (uint16_t) initialized to 0.
// Returns NULL when no more updated entries remain.
// NOTE: Declared for future use (e.g., batch republish after MQTT reconnect).
//       Not used in the initial implementation.
erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator);

// Returns the number of valid entries currently in the cache.
uint16_t erd_cache_get_count(erd_cache_t* self);

// Returns the number of cache updates that occurred in the last 60 seconds,
// then resets the window counter.
uint32_t erd_cache_get_update_rate(erd_cache_t* self);

#ifdef __cplusplus
}
#endif

#endif
