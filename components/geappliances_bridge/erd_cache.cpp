/*!
 * @file
 * @brief ERD cache implementation.
 */

#include "erd_cache.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include <cstring>
#include <new>

GEA_TAG(TAG) = "erd_cache";

static bool s_overflow_warned = false;

/* Returns true if the new data differs from the existing entry's data.
 * ERD size is invariant after registration, so only memcmp is needed.
 * Uses existing->data_size (not new_size) for the memcmp length to guard
 * against OOB reads if this function is ever called without the size check. */
static bool erd_data_changed(const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size)
{
  (void)new_size; /* Size is invariant; use existing->data_size for safety. */
  const uint8_t* old = existing->uses_heap ?
      existing->ext_data : existing->inline_data;
  return memcmp(old, new_data, existing->data_size) != 0;
}

void erd_cache_init(erd_cache_t* self)
{
  /* Set initialized=false first so that if this function is called on a
   * non-zeroed struct (stack garbage), the free loop below is skipped.
   * This makes the function safe against callers that forget memset. */
  bool was_initialized = false;
  if (self->initialized) {
    was_initialized = true;
    self->initialized = false;
  }

  /* Free heap data from a previous init.  Only runs if the cache was
   * legitimately initialized before — stack garbage is skipped above. */
  if (was_initialized) {
    for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
      erd_cache_entry_t* e = &self->entries[i];
      if (e->uses_heap) {
        delete[] e->ext_data;
      }
    }
  }
  /* Zero entries explicitly to avoid UBSan issues with bool fields after memset. */
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    e->erd = 0;
    e->data_size = 0;
    e->uses_heap = false;
    e->update_required = false;
    e->publish_cooldown = 0;
    e->valid = false;
  }
  self->update_count = 0;
  self->update_count_window = 0;
  self->required_update_count = 0;
  self->required_update_count_window = 0;
  self->max_cooldown = 0;
  self->initialized = true;
}

void erd_cache_destroy(erd_cache_t* self)
{
  if (!self->initialized) return;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->uses_heap) {
      delete[] e->ext_data;
      e->ext_data = nullptr;
      e->uses_heap = false;
    }
  }
  erd_cache_init(self);
  self->initialized = false;
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

bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size)
{
  erd_cache_entry_t* existing = erd_cache_find(self, erd);

  if (existing) {
    self->update_count++;
    self->update_count_window++;

    /* ERD size is invariant after registration.  Check size BEFORE
     * erd_data_changed to avoid reading past the old buffer when the
     * new size is larger. */
    if (data_size != existing->data_size) {
      ESP_LOGE(TAG, "ERD 0x%04X size changed %u -> %u, appliance lost",
               erd, existing->data_size, data_size);
      return false;
    }

    bool data_changed = erd_data_changed(existing, data, data_size);

    /* If data hasn't changed, skip entirely. */
    if (!data_changed) {
      return false;
    }

    /* In-place memcpy — storage tier never changes. */
    if (existing->uses_heap) {
      memcpy(existing->ext_data, data, data_size);
    } else {
      memcpy(existing->inline_data, data, data_size);
    }

    existing->update_required = data_changed;
    if (existing->update_required) {
      self->required_update_count++;
      self->required_update_count_window++;
    }
    return existing->update_required;
  }

  /* New entry — find a free slot */
  erd_cache_entry_t* slot = nullptr;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    if (!self->entries[i].valid) {
      slot = &self->entries[i];
      break;
    }
  }

  if (!slot) {
    /* Cache full — reject new ERD */
    if (!s_overflow_warned) {
      s_overflow_warned = true;
      ESP_LOGW(TAG, "ERD cache full (%u slots), new ERD 0x%04X not cached", ERD_CACHE_CAPACITY, erd);
    }
    return false;
  }

  /* Insert new entry */
  self->update_count++;
  self->update_count_window++;
  self->required_update_count++;
  self->required_update_count_window++;
  slot->erd = erd;
  slot->data_size = 0;
  slot->uses_heap = false;
  slot->valid = true;
  slot->update_required = true;
  slot->publish_cooldown = 0;

  if (data_size <= ERD_CACHE_INLINE_DATA_SIZE) {
    memcpy(slot->inline_data, data, data_size);
    slot->data_size = data_size;
    ESP_LOGD(TAG, "ERD 0x%04X added to cache (%u bytes, inline)", erd, data_size);
  } else {
    slot->ext_data = new (std::nothrow) uint8_t[data_size];
    if (slot->ext_data) {
      memcpy(slot->ext_data, data, data_size);
      slot->uses_heap = true;
      slot->data_size = data_size;
      ESP_LOGD(TAG, "ERD 0x%04X added to cache (%u bytes, heap)", erd, data_size);
    } else {
      /* Heap failed — mark entry invalid so truncated data is not published. */
      ESP_LOGW(TAG, "ERD 0x%04X heap alloc failed, entry marked invalid (data too large for inline)",
               erd);
      slot->valid = false;
      slot->update_required = false;
    }
  }

  return true;
}


void erd_cache_set_throttle_rate_seconds(erd_cache_t* self, uint8_t rate)
{
  self->max_cooldown = rate;
}

erd_cache_entry_t* erd_cache_get_next_updated(erd_cache_t* self, uint16_t* iterator)
{
  for (uint16_t i = *iterator; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (!e->valid || !e->update_required) continue;

    /* Rate limit: skip if cooldown has not expired. */
    if (self->max_cooldown > 0 && e->publish_cooldown > 0) {
      continue;  /* keep update_required=true, retry next loop */
    }

    /* Eligible — clear flag, return entry.
     * Cooldown reload happens in erd_cache_mark_published() after successful MQTT publish. */
    e->update_required = false;
    *iterator = i + 1;
    return e;
  }
  *iterator = 0; /* Reset iterator for next pass */
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

erd_cache_entry_t* erd_cache_get_next_entry(erd_cache_t* self, uint16_t* iterator)
{
  for (uint16_t i = *iterator; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid) {
      *iterator = i + 1;
      return e;
    }
  }
  *iterator = 0; /* Reset iterator for next pass */
  return nullptr;
}

uint32_t erd_cache_get_update_rate(erd_cache_t* self)
{
  uint32_t count = self->update_count_window;
  self->update_count_window = 0;
  return count;
}

uint32_t erd_cache_get_required_update_rate(erd_cache_t* self)
{
  uint32_t count = self->required_update_count_window;
  self->required_update_count_window = 0;
  return count;
}
