/*
 * @file
 * @brief Unit tests for the PollingHandler class.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#ifdef new
#undef new
#endif

#include "polling_handler.h"
#include "erd_state_table.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

TEST_GROUP(polling_handler)
{
  ErdStateTable state_table_;

  void setup() {}
  void teardown() {}
};

TEST(polling_handler, compiles_with_null_params)
{
  PollingHandler handler(nullptr, &state_table_, 10000, true);
  CHECK(!handler.is_running());
  CHECK_EQUAL(10000, handler.get_polling_interval());
  CHECK(handler.get_only_publish_on_change());
  CHECK_EQUAL(0, handler.get_polling_erds().size());
}

TEST(polling_handler, add_and_remove_erd)
{
  PollingHandler handler(nullptr, &state_table_, 10000, true);
  handler.add_erd_to_poll(0x1001);
  CHECK_EQUAL(1, handler.get_polling_erds().size());
  handler.remove_erd_from_poll(0x1001);
  CHECK_EQUAL(0, handler.get_polling_erds().size());
}

TEST(polling_handler, handle_poll_response_sets_value)
{
  PollingHandler handler(nullptr, &state_table_, 10000, true);
  uint8_t value[] = {0x01, 0x02};
  handler.handle_poll_response(0x2001, value, 2);
  uint8_t size_out;
  const uint8_t* result = state_table_.get_erd_value(0x2001, size_out);
  CHECK(result != nullptr);
  CHECK_EQUAL(2, size_out);
}

TEST(polling_handler, handle_poll_response_no_flag_on_same_value)
{
  PollingHandler handler(nullptr, &state_table_, 10000, true);
  uint8_t value[] = {0x01};
  handler.handle_poll_response(0x3001, value, 1);
  CHECK(state_table_.has_flag(0x3001));
  state_table_.clear_publish_flag(0x3001);
  handler.handle_poll_response(0x3001, value, 1);
  CHECK(!state_table_.has_flag(0x3001));
}

TEST(polling_handler, handle_poll_response_sets_flag_when_not_only_on_change)
{
  PollingHandler handler(nullptr, &state_table_, 10000, false);
  uint8_t value[] = {0x01};
  handler.handle_poll_response(0x4001, value, 1);
  state_table_.clear_publish_flag(0x4001);
  handler.handle_poll_response(0x4001, value, 1);
  CHECK(state_table_.has_flag(0x4001));
}

TEST(polling_handler, handle_poll_response_null_state_table)
{
  PollingHandler handler(nullptr, nullptr, 10000, true);
  uint8_t value[] = {0x01};
  handler.handle_poll_response(0xFFFF, value, 1);
  // Should not crash
}

TEST(polling_handler, set_polling_interval)
{
  PollingHandler handler(nullptr, &state_table_, 10000, true);
  handler.set_polling_interval(5000);
  CHECK_EQUAL(5000, handler.get_polling_interval());
}

TEST(polling_handler, handle_poll_failure_noop)
{
  PollingHandler handler(nullptr, &state_table_, 10000, true);
  handler.handle_poll_failure(0x5001);
  // Should not crash
}
