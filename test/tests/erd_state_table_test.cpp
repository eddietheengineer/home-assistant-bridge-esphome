/*
 * @file
 * @brief Unit tests for the ErdStateTable class.
 *
 * Validates value storage, publish flag semantics, change detection,
 * index table lookup, size clamping, and event firing.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "erd_state_table.h"
#include <cstring>
#include <vector>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Event callback counters                                              */
/* ------------------------------------------------------------------ */

static int erd_changed_count = 0;
static tiny_erd_t last_changed_erd = 0;

static void erd_changed_callback(void*, const void* args)
{
  erd_changed_count++;
  if (args != nullptr) {
    last_changed_erd = *static_cast<const tiny_erd_t*>(args);
  }
}

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(erd_state_table)
{
  ErdStateTable table;
  tiny_event_subscription_t sub_erd_changed_;

  void setup()
  {
    erd_changed_count = 0;
    last_changed_erd = 0;
    tiny_event_subscription_init(&sub_erd_changed_, nullptr, erd_changed_callback);
    tiny_event_subscribe(table.on_erd_changed(), &sub_erd_changed_);
  }

  void teardown()
  {
    tiny_event_unsubscribe(table.on_erd_changed(), &sub_erd_changed_);
  }
};

/* ------------------------------------------------------------------ */
/* Basic value storage and retrieval                                   */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, update_and_retrieve_value)
{
  uint8_t data[] = {0x01, 0x02, 0x03};
  table.update_erd_value(0x1001, data, 3);

  uint8_t size_out;
  const uint8_t* result = table.get_erd_value(0x1001, size_out);

  CHECK(result != nullptr);
  CHECK_EQUAL(3, size_out);
  MEMCMP_EQUAL(data, result, 3);
}

TEST(erd_state_table, get_nonexistent_erd_returns_null)
{
  uint8_t size_out;
  CHECK_EQUAL(nullptr, table.get_erd_value(0xFFFF, size_out));
  CHECK_EQUAL(0, size_out);
}

/* ------------------------------------------------------------------ */
/* Publish flag — change detection                                     */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, publish_flag_set_on_new_erd)
{
  uint8_t data[] = {0xAA};
  table.update_erd_value(0x2001, data, 1);

  CHECK(table.has_flag(0x2001));
}

TEST(erd_state_table, publish_flag_set_only_when_value_changes)
{
  uint8_t data[] = {0x01};
  table.update_erd_value(0x3001, data, 1);
  CHECK(table.has_flag(0x3001));

  table.clear_publish_flag(0x3001);
  CHECK(!table.has_flag(0x3001));

  // Same value — should NOT set flag
  table.update_erd_value(0x3001, data, 1);
  CHECK(!table.has_flag(0x3001));
}

TEST(erd_state_table, publish_flag_set_when_value_differs)
{
  uint8_t data1[] = {0x01};
  uint8_t data2[] = {0x02};
  table.update_erd_value(0x4001, data1, 1);
  table.clear_publish_flag(0x4001);

  table.update_erd_value(0x4001, data2, 1);
  CHECK(table.has_flag(0x4001));
}

TEST(erd_state_table, publish_flag_set_when_size_differs)
{
  uint8_t data1[] = {0x01};
  uint8_t data2[] = {0x01, 0x02};
  table.update_erd_value(0x5001, data1, 1);
  table.clear_publish_flag(0x5001);

  table.update_erd_value(0x5001, data2, 2);
  CHECK(table.has_flag(0x5001));
}

/* ------------------------------------------------------------------ */
/* set_publish_flag — unconditional                                    */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, set_publish_flag_unconditional)
{
  uint8_t data[] = {0x01};
  table.update_erd_value(0x6001, data, 1);
  table.clear_publish_flag(0x6001);

  table.set_publish_flag(0x6001);
  CHECK(table.has_flag(0x6001));
}

TEST(erd_state_table, set_publish_flag_noop_for_unknown_erd)
{
  table.set_publish_flag(0xFFFF);
  CHECK(!table.has_flag(0xFFFF));
}

/* ------------------------------------------------------------------ */
/* get_flagged_erds                                                    */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, get_flagged_erds_returns_only_flagged)
{
  uint8_t d1[] = {0x01};
  uint8_t d2[] = {0x02};
  uint8_t d3[] = {0x03};
  table.update_erd_value(0x7001, d1, 1);
  table.update_erd_value(0x7002, d2, 1);
  table.update_erd_value(0x7003, d3, 1);

  table.clear_publish_flag(0x7002);

  std::vector<tiny_erd_t> flagged = table.get_flagged_erds();
  CHECK_EQUAL(2, flagged.size());
  CHECK(flagged[0] == 0x7001 || flagged[0] == 0x7003);
  CHECK(flagged[1] == 0x7001 || flagged[1] == 0x7003);
}

TEST(erd_state_table, get_flagged_erds_empty_when_none_flagged)
{
  uint8_t data[] = {0x01};
  table.update_erd_value(0x8001, data, 1);
  table.clear_publish_flag(0x8001);

  std::vector<tiny_erd_t> flagged = table.get_flagged_erds();
  CHECK_EQUAL(0, flagged.size());
}

/* ------------------------------------------------------------------ */
/* Event firing                                                        */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, on_erd_changed_fires_on_new_value)
{
  uint8_t data[] = {0x01};
  table.update_erd_value(0x9001, data, 1);

  CHECK_EQUAL(1, erd_changed_count);
  CHECK_EQUAL(0x9001, last_changed_erd);
}

TEST(erd_state_table, on_erd_changed_does_not_fire_on_same_value)
{
  uint8_t data[] = {0x01};
  table.update_erd_value(0xA001, data, 1);
  erd_changed_count = 0;  // reset

  table.update_erd_value(0xA001, data, 1);
  CHECK_EQUAL(0, erd_changed_count);
}

TEST(erd_state_table, on_erd_changed_fires_on_value_change)
{
  uint8_t data1[] = {0x01};
  uint8_t data2[] = {0x02};
  table.update_erd_value(0xB001, data1, 1);
  erd_changed_count = 0;

  table.update_erd_value(0xB001, data2, 1);
  CHECK_EQUAL(1, erd_changed_count);
  CHECK_EQUAL(0xB001, last_changed_erd);
}

/* ------------------------------------------------------------------ */
/* Size clamping                                                       */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, clamps_value_size_to_max)
{
  uint8_t data[64];
  std::memset(data, 0xFF, 64);
  table.update_erd_value(0xC001, data, 64);

  uint8_t size_out;
  const uint8_t* result = table.get_erd_value(0xC001, size_out);
  CHECK(result != nullptr);
  CHECK_EQUAL(static_cast<uint8_t>(MAX_ERD_VALUE_SIZE), size_out);
}

/* ------------------------------------------------------------------ */
/* Multiple ERDs                                                       */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, stores_multiple_erds_independently)
{
  uint8_t d1[] = {0x01, 0x02};
  uint8_t d2[] = {0x03, 0x04, 0x05};
  table.update_erd_value(0xD001, d1, 2);
  table.update_erd_value(0xD002, d2, 3);

  uint8_t size_out;
  const uint8_t* v1 = table.get_erd_value(0xD001, size_out);
  CHECK_EQUAL(2, size_out);
  MEMCMP_EQUAL(d1, v1, 2);

  const uint8_t* v2 = table.get_erd_value(0xD002, size_out);
  CHECK_EQUAL(3, size_out);
  MEMCMP_EQUAL(d2, v2, 3);
}

/* ------------------------------------------------------------------ */
/* has_flag for unknown ERD                                            */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, has_flag_returns_false_for_unknown_erd)
{
  CHECK(!table.has_flag(0xFFFF));
}

/* ------------------------------------------------------------------ */
/* clear_publish_flag for unknown ERD                                  */
/* ------------------------------------------------------------------ */

TEST(erd_state_table, clear_publish_flag_noop_for_unknown_erd)
{
  table.clear_publish_flag(0xFFFF);
  CHECK(!table.has_flag(0xFFFF));
}
