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

/* Pool block sizes indexed by pool_block_idx (0-1). */
static const uint8_t pool_block_sizes[] = {
  ERD_CACHE_POOL_BLOCK_1,
  ERD_CACHE_POOL_BLOCK_2
};

/* Returns the pool block index for a given data size, or 255 if inline or too large.
 * Data <= 4 bytes is stored inline (no pool). Data > 32 bytes goes to heap. */
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
  /* Validate that the pointer actually points into the pool.
   * Compute bounds using byte arithmetic to avoid UBSan out-of-bounds
   * access on the one-past-the-end pointer. */
  uint8_t* base = &self->pool_blocks[block_idx][0][0];
  size_t pool_bytes = (size_t)ERD_CACHE_CAPACITY * pool_block_sizes[block_idx];
  if (ptr < base || (size_t)(ptr - base) >= pool_bytes) {
    ESP_LOGE(TAG, "pool_free: pointer 0x%08X outside pool bounds [0x%08X, 0x%08X)",
             (unsigned)(uintptr_t)ptr, (unsigned)(uintptr_t)base,
             (unsigned)(uintptr_t)(base + pool_bytes));
    return;
  }
  /* Find the slot by pointer arithmetic. */
  ptrdiff_t offset = ptr - base;
  uint16_t slot = (uint16_t)(offset / pool_block_sizes[block_idx]);
  if (slot < ERD_CACHE_CAPACITY) {
    self->pool_free[block_idx][slot] = true;
  }
}

/* Assign storage for new data into an entry.
 * Tries inline first, then pool, then heap, falling back to truncation.
 * Returns true on success, false if all storage failed (truncation applied). */
static bool store_data(erd_cache_t* self, erd_cache_entry_t* entry,
                       const uint8_t* data, uint8_t data_size, [[maybe_unused]] tiny_erd_t erd)
{
  if (data_size <= ERD_CACHE_INLINE_DATA_SIZE) {
    memcpy(entry->inline_data, data, data_size);
    entry->data_size = data_size;
    return true;
  }

  uint8_t block_idx = pool_block_for_size(data_size);
  if (block_idx != 255) {
    uint8_t* buf = pool_alloc(self, block_idx);
    if (buf) {
      memcpy(buf, data, data_size);
      entry->ext_data = buf;
      entry->uses_pool = true;
      entry->pool_block_idx = block_idx;
      entry->ext_alloc_size = pool_block_sizes[block_idx];
      entry->data_size = data_size;
      return true;
    }
  }

  /* Pool exhausted or data too large — try heap. */
  entry->ext_data = new (std::nothrow) uint8_t[data_size];
  if (entry->ext_data) {
    memcpy(entry->ext_data, data, data_size);
    entry->uses_heap = true;
    entry->ext_alloc_size = data_size;
    entry->data_size = data_size;
    return true;
  }

  /* Heap failed — truncate to inline. */
  ESP_LOGW(TAG, "ERD 0x%04X all storage exhausted, truncating to %u bytes",
           erd, ERD_CACHE_INLINE_DATA_SIZE);
  uint8_t inline_size = ERD_CACHE_INLINE_DATA_SIZE;
  memcpy(entry->inline_data, data, inline_size);
  entry->data_size = inline_size;
  return false;
}

/* Free the current storage of an entry (pool, heap, or both). */
static void free_entry_storage(erd_cache_t* self, erd_cache_entry_t* entry)
{
  if (entry->uses_pool) {
    pool_free(self, entry->pool_block_idx, entry->ext_data);
    entry->uses_pool = false;
    entry->pool_block_idx = 255;
  }
  if (entry->uses_heap) {
    delete[] entry->ext_data;
    entry->uses_heap = false;
    entry->ext_alloc_size = 0;
  }
  entry->ext_data = NULL;
}
/* Returns true if the new data differs from the existing entry's data.
 * Compares size first (fast path), then does a full memcmp of the shared
 * length when sizes are equal.  This avoids partial memcmp of mismatched
 * lengths — the size check alone catches those cases. */
static bool erd_data_changed(const erd_cache_entry_t* existing,
                             const uint8_t* new_data, uint8_t new_size)
{
  if (existing->data_size != new_size) return true;
  const uint8_t* old = (existing->uses_heap || existing->uses_pool) ? existing->ext_data : existing->inline_data;
  return memcmp(old, new_data, new_size) != 0;
}

void erd_cache_init(erd_cache_t* self)
{
  /* Free any pool or heap data before resetting.
   * Only do this if the cache was previously initialized — on a fresh
   * stack-allocated struct the flags are garbage and could trigger
   * delete[] on a random pointer. */
  if (self->initialized) {
    for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
      erd_cache_entry_t* e = &self->entries[i];
      if (e->uses_pool) {
        pool_free(self, e->pool_block_idx, e->ext_data);
      }
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
    e->ext_alloc_size = 0;
    e->pool_block_idx = 255;
    e->uses_pool = false;
    e->uses_heap = false;
    e->update_required = false;
    e->valid = false;
    e->ext_data = NULL;
  }
  self->update_count = 0;
  self->update_count_window = 0;
  self->required_update_count = 0;
  self->required_update_count_window = 0;
  self->only_publish_onchange = false;
  /* Zero pool blocks and mark all as free. */
  (void)memset(self->pool_blocks, 0, sizeof(self->pool_blocks));
  for (uint8_t b = 0; b < ERD_CACHE_POOL_COUNT; b++) {
    for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
      self->pool_free[b][i] = 1;
    }
  }
  self->initialized = true;
}

void erd_cache_destroy(erd_cache_t* self)
{
  if (!self->initialized) return;
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
    bool data_changed = erd_data_changed(existing, data, data_size);

    /* Free old storage before assigning new.
     * Exception: if we're staying on heap and the existing buffer is large
     * enough, reuse it in place to avoid new/delete churn. */
    bool can_reuse_heap = existing->uses_heap &&
                          data_size <= existing->ext_alloc_size &&
                          data_size > ERD_CACHE_INLINE_DATA_SIZE;

    if (!can_reuse_heap) {
      free_entry_storage(self, existing);
    }

    if (can_reuse_heap) {
      /* Reuse existing heap buffer — no allocation needed. */
      memcpy(existing->ext_data, data, data_size);
      existing->data_size = data_size;
    } else {
      /* Assign new storage: inline → pool → heap → truncate. */
      bool ok = store_data(self, existing, data, data_size, erd);
      if (!ok) {
        /* Truncation applied — always publish truncated data. */
        existing->update_required = true;
        self->required_update_count++;
        self->required_update_count_window++;
        return true;
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
  slot->data_size = 0;
  slot->uses_pool = false;
  slot->uses_heap = false;
  slot->pool_block_idx = 255;
  slot->ext_alloc_size = 0;
  slot->valid = true;
  slot->update_required = true;

  bool ok = store_data(self, slot, data, data_size, erd);
  if (ok) {
    if (slot->uses_heap) {
      ESP_LOGD(TAG, "ERD 0x%04X added to cache (%u bytes, heap)", erd, data_size);
    } else if (slot->uses_pool) {
      ESP_LOGD(TAG, "ERD 0x%04X added to cache (%u bytes, pool)", erd, data_size);
    } else {
      ESP_LOGD(TAG, "ERD 0x%04X added to cache (%u bytes, inline)", erd, data_size);
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
