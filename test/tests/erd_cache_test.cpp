/*!
 * @file
 * @brief Unit tests for the ERD cache (erd_cache.h / erd_cache.cpp).
 *
 * Tests cover: init/destroy, inline vs heap storage, update flow,
 * change detection, only_publish_onchange, iterators, rate counters,
 * cache full, size change detection.
 *
 * Note: erd_cache_find() is internal (static in .cpp), so tests use
 * erd_cache_get_next_entry() to locate entries by iterating the cache.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTest/MemoryLeakDetectorNewMacros.h"

extern "C" {
#include "erd_cache.h"
}

// Helper: find an entry by iterating the cache (erd_cache_find is internal)
static erd_cache_entry_t* find_entry(erd_cache_t* cache, tiny_erd_t erd)
{
  uint16_t iter = 0;
  erd_cache_entry_t* e;
  while ((e = erd_cache_get_next_entry(cache, &iter)) != NULL) {
    if (e->erd == erd) return e;
  }
  return NULL;
}

TEST_GROUP(erd_cache)
{
  erd_cache_t cache;

  void setup()
  {
    erd_cache_init(&cache);
  }

  void teardown()
  {
    erd_cache_destroy(&cache);
  }
};

TEST(erd_cache, init_zeros_all_entries)
{
  CHECK_TRUE(cache.initialized);
  CHECK_EQUAL(0, cache.update_count);
  CHECK_EQUAL(0, cache.update_count_window);
  CHECK_EQUAL(0, cache.required_update_count);
  CHECK_EQUAL(0, cache.required_update_count_window);
  CHECK_FALSE(cache.only_publish_onchange);
  CHECK_EQUAL(0, erd_cache_get_count(&cache));
}

TEST(erd_cache, destroy_after_init)
{
  erd_cache_destroy(&cache);
  CHECK_FALSE(cache.initialized);
}

TEST(erd_cache, destroy_on_uninitialized_is_safe)
{
  erd_cache_t uninit;
  uninit.initialized = false;
  erd_cache_destroy(&uninit);
}

TEST(erd_cache, insert_inline_1_byte)
{
  uint8_t data[] = { 0xAB };
  bool result = erd_cache_update(&cache, 0x0001, data, 1);
  CHECK_TRUE(result);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_FALSE(entry->uses_heap);
  CHECK_EQUAL(1, entry->data_size);
  CHECK_EQUAL(0xAB, entry->inline_data[0]);
  CHECK_TRUE(entry->update_required);
  CHECK_TRUE(entry->valid);
}

TEST(erd_cache, insert_inline_4_bytes)
{
  uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
  bool result = erd_cache_update(&cache, 0x0010, data, 4);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0010);
  CHECK(NULL != entry);
  CHECK_FALSE(entry->uses_heap);
  CHECK_EQUAL(4, entry->data_size);
  MEMCMP_EQUAL(data, entry->inline_data, 4);
}

TEST(erd_cache, insert_heap_5_bytes)
{
  uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  bool result = erd_cache_update(&cache, 0x0020, data, 5);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0020);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->uses_heap);
  CHECK_EQUAL(5, entry->data_size);
  MEMCMP_EQUAL(data, entry->ext_data, 5);
}

TEST(erd_cache, insert_heap_large)
{
  uint8_t data[32];
  for (int i = 0; i < 32; i++) data[i] = (uint8_t)i;
  bool result = erd_cache_update(&cache, 0x0100, data, 32);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0100);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->uses_heap);
  CHECK_EQUAL(32, entry->data_size);
  MEMCMP_EQUAL(data, entry->ext_data, 32);
}

TEST(erd_cache, update_existing_inline)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  bool result = erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_EQUAL(0x02, entry->inline_data[0]);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, update_existing_heap)
{
  uint8_t data1[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  uint8_t data2[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
  erd_cache_update(&cache, 0x0020, data1, 5);
  bool result = erd_cache_update(&cache, 0x0020, data2, 5);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0020);
  CHECK(NULL != entry);
  MEMCMP_EQUAL(data2, entry->ext_data, 5);
}

TEST(erd_cache, size_change_grows_returns_false)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x01, 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  bool result = erd_cache_update(&cache, 0x0001, data2, 2);
  CHECK_FALSE(result);
}

TEST(erd_cache, size_change_shrinks_returns_false)
{
  uint8_t data1[] = { 0x01, 0x02 };
  uint8_t data2[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data1, 2);
  bool result = erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_FALSE(result);
}

TEST(erd_cache, only_publish_onchange_true_unchanged_data)
{
  uint8_t data[] = { 0x01 };
  erd_cache_set_only_publish_onchange(&cache, true);
  erd_cache_update(&cache, 0x0001, data, 1);

  // Unchanged data returns false and does NOT clear update_required
  // (early exit before the update_required assignment at line 116)
  bool result = erd_cache_update(&cache, 0x0001, data, 1);
  CHECK_FALSE(result);

  // update_required remains true from the initial insert
  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, only_publish_onchange_true_changed_data)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_set_only_publish_onchange(&cache, true);
  erd_cache_update(&cache, 0x0001, data1, 1);

  bool result = erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, only_publish_onchange_false_always_sets_update_required)
{
  uint8_t data[] = { 0x01 };
  erd_cache_set_only_publish_onchange(&cache, false);
  erd_cache_update(&cache, 0x0001, data, 1);

  bool result = erd_cache_update(&cache, 0x0001, data, 1);
  CHECK_TRUE(result);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, get_next_updated_returns_updated_entries)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* e1 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e1);
  CHECK_EQUAL(0x0001, e1->erd);
  CHECK_FALSE(e1->update_required);

  erd_cache_entry_t* e2 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e2);
  CHECK_EQUAL(0x0002, e2->erd);

  erd_cache_entry_t* e3 = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL == e3);
}

TEST(erd_cache, get_next_updated_skips_non_updated)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  uint8_t data3[] = { 0x03 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);
  erd_cache_update(&cache, 0x0003, data3, 1);

  erd_cache_entry_t* e2 = find_entry(&cache, 0x0002);
  CHECK(NULL != e2);
  e2->update_required = false;

  uint16_t iter = 0;
  erd_cache_entry_t* e = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e);
  CHECK_EQUAL(0x0001, e->erd);

  e = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL != e);
  CHECK_EQUAL(0x0003, e->erd);

  e = erd_cache_get_next_updated(&cache, &iter);
  CHECK(NULL == e);
}

TEST(erd_cache, get_next_updated_resets_iterator)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_get_next_updated(&cache, &iter);
  erd_cache_get_next_updated(&cache, &iter);
  CHECK_EQUAL(0, iter);
}

TEST(erd_cache, get_next_entry_iterates_all_valid)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0002, data2, 1);

  uint16_t iter = 0;
  erd_cache_entry_t* e1 = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL != e1);
  CHECK_EQUAL(0x0001, e1->erd);
  CHECK_TRUE(e1->update_required);

  erd_cache_entry_t* e2 = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL != e2);
  CHECK_EQUAL(0x0002, e2->erd);

  erd_cache_entry_t* e3 = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL == e3);
}

TEST(erd_cache, get_count_returns_valid_entry_count)
{
  CHECK_EQUAL(0, erd_cache_get_count(&cache));

  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));

  erd_cache_update(&cache, 0x0002, data2, 1);
  CHECK_EQUAL(2, erd_cache_get_count(&cache));
}

TEST(erd_cache, get_update_rate)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0002, data, 1);

  CHECK_EQUAL(2, erd_cache_get_update_rate(&cache));
  CHECK_EQUAL(0, erd_cache_get_update_rate(&cache));

  erd_cache_update(&cache, 0x0003, data, 1);
  CHECK_EQUAL(1, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache, get_required_update_rate)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0002, data, 1);

  CHECK_EQUAL(2, erd_cache_get_required_update_rate(&cache));
  CHECK_EQUAL(0, erd_cache_get_required_update_rate(&cache));
}

TEST(erd_cache, required_update_rate_with_only_publish_onchange)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_set_only_publish_onchange(&cache, true);
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0001, data1, 1);

  CHECK_EQUAL(1, erd_cache_get_required_update_rate(&cache));

  erd_cache_update(&cache, 0x0001, data2, 1);
  CHECK_EQUAL(1, erd_cache_get_required_update_rate(&cache));
}

TEST(erd_cache, cache_full_rejects_new_erd)
{
  for (int i = 0; i < ERD_CACHE_CAPACITY; i++) {
    uint8_t data[] = { (uint8_t)i };
    tiny_erd_t erd = (tiny_erd_t)(0x0001 + i);
    bool result = erd_cache_update(&cache, erd, data, 1);
    CHECK_TRUE(result);
  }
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_count(&cache));

  uint8_t data[] = { 0xFF };
  bool result = erd_cache_update(&cache, 0xFFFF, data, 1);
  CHECK_FALSE(result);
}

TEST(erd_cache, find_returns_null_for_unknown_erd)
{
  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL == entry);
}

TEST(erd_cache, destroy_frees_heap_data)
{
  uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
  erd_cache_update(&cache, 0x0020, data, 5);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0020);
  CHECK(NULL != entry);
  CHECK(NULL != entry->ext_data);

  erd_cache_destroy(&cache);
  CHECK_FALSE(cache.initialized);
}

TEST(erd_cache, reinit_after_destroy)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_destroy(&cache);

  erd_cache_init(&cache);
  CHECK_EQUAL(0, erd_cache_get_count(&cache));

  erd_cache_update(&cache, 0x0002, data, 1);
  CHECK_EQUAL(1, erd_cache_get_count(&cache));
}

TEST(erd_cache, new_entry_always_sets_update_required)
{
  uint8_t data[] = { 0x01 };
  erd_cache_set_only_publish_onchange(&cache, true);
  erd_cache_update(&cache, 0x0001, data, 1);

  erd_cache_entry_t* entry = find_entry(&cache, 0x0001);
  CHECK(NULL != entry);
  CHECK_TRUE(entry->update_required);
}

TEST(erd_cache, get_next_entry_resets_iterator_on_exhaust)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);

  uint16_t iter = 0;
  erd_cache_get_next_entry(&cache, &iter);
  erd_cache_get_next_entry(&cache, &iter);
  CHECK_EQUAL(0, iter);

  erd_cache_entry_t* e = erd_cache_get_next_entry(&cache, &iter);
  CHECK(NULL != e);
  CHECK_EQUAL(0x0001, e->erd);
}

TEST(erd_cache, update_count_increments_on_existing_update)
{
  uint8_t data1[] = { 0x01 };
  uint8_t data2[] = { 0x02 };
  erd_cache_update(&cache, 0x0001, data1, 1);
  erd_cache_update(&cache, 0x0001, data2, 1);

  CHECK_EQUAL(2, cache.update_count);
}

TEST(erd_cache, update_count_increments_on_new_entry)
{
  uint8_t data[] = { 0x01 };
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0002, data, 1);
  erd_cache_update(&cache, 0x0003, data, 1);

  CHECK_EQUAL(3, cache.update_count);
}

TEST(erd_cache, unchanged_data_does_not_increment_required_count)
{
  uint8_t data[] = { 0x01 };
  erd_cache_set_only_publish_onchange(&cache, true);
  erd_cache_update(&cache, 0x0001, data, 1);
  erd_cache_update(&cache, 0x0001, data, 1);

  CHECK_EQUAL(1, cache.required_update_count);
}
