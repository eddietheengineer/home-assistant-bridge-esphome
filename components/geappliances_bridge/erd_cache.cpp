/*!
 * @file
 * @brief ERD cache implementation.
 */

#include "erd_cache.h"
#include "esphome/core/log.h"

#include <string.h>
#include <new>

static const char* const TAG = "erd_cache";

static bool s_overflow_warned = false;

void erd_cache_init(erd_cache_t* self)
{
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->uses_heap) {
      delete[] e->heap_data;
      e->heap_data = nullptr;
    }
  }
  (void)memset(self, 0, sizeof(*self));
}

void erd_cache_destroy(erd_cache_t* self)
{
  erd_cache_init(self);
}

erd_cache_entry_t* erd_cache_find(erd_cache_t* self, tiny_erd_t erd)
{
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->erd == erd) {
      return e;
    }
  }
  return nullptr;
}

bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size, bool is_subscription)
{
  // Look for existing entry
  erd_cache_entry_t* existing = nullptr;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->erd == erd) {
      existing = e;
      break;
    }
  }

  if (existing) {
    self->update_count++;
    self->update_count_window++;
    // Update existing entry
    bool data_changed = (existing->data_size != data_size) ||
                        (memcmp(existing->uses_heap ? existing->heap_data : existing->inline_data,
                                data, data_size) != 0);

    bool needs_heap = data_size > ERD_CACHE_INLINE_DATA_SIZE;

    // Free old heap data if currently on heap
    if (existing->uses_heap) {
      delete[] existing->heap_data;
    }

    // Store new data
    if (needs_heap) {
      existing->heap_data = new (std::nothrow) uint8_t[data_size];
      if (!existing->heap_data) {
        ESP_LOGW(TAG, "Failed to allocate %u bytes for ERD 0x%04X", data_size, erd);
        existing->uses_heap = false;
        memcpy(existing->inline_data, data, (data_size < ERD_CACHE_INLINE_DATA_SIZE) ? data_size : ERD_CACHE_INLINE_DATA_SIZE);
        existing->data_size = (data_size < ERD_CACHE_INLINE_DATA_SIZE) ? data_size : ERD_CACHE_INLINE_DATA_SIZE;
        return false;
      }
      memcpy(existing->heap_data, data, data_size);
    } else {
      memcpy(existing->inline_data, data, data_size);
    }

    existing->data_size = data_size;
    existing->uses_heap = needs_heap;

    if (is_subscription) {
      existing->update_required = true;
      return true;
    }

    existing->update_required = data_changed;
    return data_changed;
  }

  // New entry — find a free slot
  erd_cache_entry_t* slot = nullptr;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    if (!self->entries[i].valid) {
      slot = &self->entries[i];
      break;
    }
  }

  if (!slot) {
    // Cache full — reject new ERD
    if (!s_overflow_warned) {
      s_overflow_warned = true;
      ESP_LOGW(TAG, "ERD cache full (%u slots), new ERD 0x%04X not cached", ERD_CACHE_CAPACITY, erd);
    }
    return false;
  }

  // Insert new entry
  bool needs_heap = data_size > ERD_CACHE_INLINE_DATA_SIZE;
  self->update_count++;
  self->update_count_window++;
  slot->erd = erd;
  slot->data_size = data_size;
  slot->uses_heap = needs_heap;
  slot->valid = true;
  slot->update_required = true;

  if (needs_heap) {
    slot->heap_data = new (std::nothrow) uint8_t[data_size];
    if (!slot->heap_data) {
      ESP_LOGW(TAG, "Failed to allocate %u bytes for ERD 0x%04X", data_size, erd);
      slot->uses_heap = false;
      memcpy(slot->inline_data, data, (data_size < ERD_CACHE_INLINE_DATA_SIZE) ? data_size : ERD_CACHE_INLINE_DATA_SIZE);
      slot->data_size = (data_size < ERD_CACHE_INLINE_DATA_SIZE) ? data_size : ERD_CACHE_INLINE_DATA_SIZE;
    } else {
      memcpy(slot->heap_data, data, data_size);
    }
  } else {
    memcpy(slot->inline_data, data, data_size);
  }

  return true;
}

erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator)
{
  for (uint16_t i = *iterator; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->update_required) {
      e->update_required = false;
      *iterator = i + 1;
      return e;
    }
  }
  *iterator = 0; // Reset iterator for next pass
  return nullptr;
}

uint16_t erd_cache_get_count(erd_cache_t* self)
{
  uint16_t count = 0;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    if (self->entries[i].valid) {
      count++;
    }
  }
  return count;
}

uint32_t erd_cache_get_update_rate(erd_cache_t* self)
{
  uint32_t count = self->update_count_window;
  self->update_count_window = 0;
  return count;
}
