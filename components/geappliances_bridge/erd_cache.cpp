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

/* Pool block sizes indexed by pool_block_idx (0-3). */
static const uint8_t pool_block_sizes[] = {
  ERD_CACHE_POOL_BLOCK_1,
  ERD_CACHE_POOL_BLOCK_2,
  ERD_CACHE_POOL_BLOCK_3,
  ERD_CACHE_POOL_BLOCK_4
};

/* Returns the pool block index for a given data size, or 255 if inline or too large.
 * Data <= 16 bytes is stored inline (no pool). */
static uint8_t pool_block_for_size(uint8_t data_size)
{
  if (data_size <= ERD_CACHE_INLINE_DATA_SIZE) {
    return 255;
  }
  for (uint8_t i = 0; i < ERD_CACHE_POOL_COUNT; i++) {
    if (data_size <= pool_block_sizes[i]) {
      return i;
    }
  }
  return 255;
}

/* Allocate a block from the pool. Returns pointer on success, NULL on failure. */
static uint8_t* pool_alloc(erd_cache_t* self, uint8_t block_idx)
{
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    if (self->pool_free[block_idx][i]) {
      self->pool_free[block_idx][i] = false;
      return &self->pool_blocks[block_idx][i][0];
    }
  }
  return NULL;
}

/* Return a block to the pool. */
static void pool_free(erd_cache_t* self, uint8_t block_idx, uint8_t* ptr)
{
  if (!ptr) return;
  /* Find the slot by pointer arithmetic. */
  uint8_t* base = &self->pool_blocks[block_idx][0][0];
  ptrdiff_t offset = ptr - base;
  uint16_t slot = (uint16_t)(offset / pool_block_sizes[block_idx]);
  if (slot < ERD_CACHE_CAPACITY) {
    self->pool_free[block_idx][slot] = true;
  }
}

/* Returns true if the new data differs from the existing entry's data.
 * Compares size first (fast path), then does a full memcmp of the shared
 * length when sizes are equal.  This avoids partial memcmp of mismatched
 * lengths — the size check alone catches those cases. */
static bool erd_data_changed(const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size)
{
  if (existing->data_size != new_size) return true;
  const uint8_t* old = (existing->uses_heap ? existing->ext_data : (existing->uses_pool ? existing->ext_data : existing->inline_data));
  return memcmp(old, new_data, new_size) != 0;
}

void erd_cache_init(erd_cache_t* self)
{
  /* Free any pool or heap data before zeroing the struct. */
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_entry_t* e = &self->entries[i];
    if (e->valid && e->uses_pool) {
      pool_free(self, e->pool_block_idx, e->ext_data);
    }
    if (e->valid && e->uses_heap) {
      delete[] e->ext_data;
    }
  }
  (void)memset(self, 0, sizeof(*self));
  /* Mark all pool blocks as free. */
  for (uint8_t b = 0; b < ERD_CACHE_POOL_COUNT; b++) {
    for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
      self->pool_free[b][i] = true;
    }
  }
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

bool erd_cache_update(erd_cache_t* self, tiny_erd_t erd, const uint8_t* data, uint8_t data_size)
{
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
    bool data_changed = erd_data_changed(existing, data, data_size);

    /* Free old external data if currently on pool or heap. */
    if (existing->uses_pool) {
      pool_free(self, existing->pool_block_idx, existing->ext_data);
      existing->uses_pool = false;
      existing->pool_block_idx = 255;
    }
    if (existing->uses_heap) {
      delete[] existing->ext_data;
      existing->uses_heap = false;
    }

    /* Determine storage strategy: inline, pool, or heap. */
    if (data_size <= ERD_CACHE_INLINE_DATA_SIZE) {
      /* Data fits inline — store directly. */
      memcpy(existing->inline_data, data, data_size);
      existing->data_size = data_size;
    } else {
      /* Try pool first, then heap as fallback. */
      uint8_t block_idx = pool_block_for_size(data_size);
      if (block_idx != 255) {
        uint8_t* buf = pool_alloc(self, block_idx);
        if (buf) {
          memcpy(buf, data, data_size);
          existing->ext_data = buf;
          existing->uses_pool = true;
          existing->pool_block_idx = block_idx;
          existing->data_size = data_size;
        } else {
          /* Pool exhausted — fall back to heap. */
          existing->ext_data = new (std::nothrow) uint8_t[data_size];
          if (existing->ext_data) {
            memcpy(existing->ext_data, data, data_size);
            existing->uses_heap = true;
            existing->data_size = data_size;
          } else {
            /* Heap also failed — truncate to inline. */
            uint8_t inline_size = ERD_CACHE_INLINE_DATA_SIZE;
            memcpy(existing->inline_data, data, inline_size);
            existing->data_size = inline_size;
            existing->update_required = true;
            self->required_update_count++;
            self->required_update_count_window++;
            return true;
          }
        }
      } else {
        /* Data too large for pool — use heap. */
        existing->ext_data = new (std::nothrow) uint8_t[data_size];
        if (existing->ext_data) {
          memcpy(existing->ext_data, data, data_size);
          existing->uses_heap = true;
          existing->data_size = data_size;
        } else {
          /* Heap failed — truncate to inline. */
          uint8_t inline_size = ERD_CACHE_INLINE_DATA_SIZE;
          memcpy(existing->inline_data, data, inline_size);
          existing->data_size = inline_size;
          existing->update_required = true;
          self->required_update_count++;
          self->required_update_count_window++;
          return true;
        }
      }
    }

    existing->update_required = !self->only_publish_onchange || data_changed;
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
  slot->data_size = data_size;
  slot->uses_pool = false;
  slot->uses_heap = false;
  slot->pool_block_idx = 255;
  slot->valid = true;
  slot->update_required = true;
  ESP_LOGD(TAG, "ERD 0x%04X added to cache (%u bytes)", erd, data_size);

  if (data_size <= ERD_CACHE_INLINE_DATA_SIZE) {
    /* Data fits inline — store directly. */
    memcpy(slot->inline_data, data, data_size);
    slot->data_size = data_size;
  } else {
    /* Try pool first, then heap as fallback. */
    uint8_t block_idx = pool_block_for_size(data_size);
    if (block_idx != 255) {
      uint8_t* buf = pool_alloc(self, block_idx);
      if (buf) {
        memcpy(buf, data, data_size);
        slot->ext_data = buf;
        slot->uses_pool = true;
        slot->pool_block_idx = block_idx;
        slot->data_size = data_size;
      } else {
        /* Pool exhausted — fall back to heap. */
        slot->ext_data = new (std::nothrow) uint8_t[data_size];
        if (slot->ext_data) {
          memcpy(slot->ext_data, data, data_size);
          slot->uses_heap = true;
          slot->data_size = data_size;
        } else {
          /* Heap also failed — truncate to inline. */
          ESP_LOGW(TAG, "Pool and heap exhausted for ERD 0x%04X (%u bytes), truncating to %u bytes",
                   erd, data_size, ERD_CACHE_INLINE_DATA_SIZE);
          uint8_t inline_size = ERD_CACHE_INLINE_DATA_SIZE;
          memcpy(slot->inline_data, data, inline_size);
          slot->data_size = inline_size;
        }
      }
    } else {
      /* Data too large for pool — use heap. */
      slot->ext_data = new (std::nothrow) uint8_t[data_size];
      if (slot->ext_data) {
        memcpy(slot->ext_data, data, data_size);
        slot->uses_heap = true;
        slot->data_size = data_size;
      } else {
        /* Heap failed — truncate to inline. */
        ESP_LOGW(TAG, "Heap allocation failed for ERD 0x%04X (%u bytes), truncating to %u bytes",
                 erd, data_size, ERD_CACHE_INLINE_DATA_SIZE);
        uint8_t inline_size = ERD_CACHE_INLINE_DATA_SIZE;
        memcpy(slot->inline_data, data, inline_size);
        slot->data_size = inline_size;
      }
    }
  }

  return true;
}

void erd_cache_set_only_publish_onchange(erd_cache_t* self, bool only_publish_onchange)
{
  self->only_publish_onchange = only_publish_onchange;
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
