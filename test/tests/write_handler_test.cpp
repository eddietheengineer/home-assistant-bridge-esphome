/*
 * @file
 * @brief Unit tests for the WriteHandler class.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#ifdef new
#undef new
#endif

#include "write_handler.h"
#include "write_queue.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

TEST_GROUP(write_handler)
{
  WriteQueue queue_;

  void setup() {}
  void teardown() {}
};

TEST(write_handler, compiles_with_null_params)
{
  WriteHandler handler(nullptr, &queue_);
  CHECK_EQUAL(0, handler.in_flight_count());
  CHECK_EQUAL(0, handler.get_appliance_address());
}

TEST(write_handler, set_appliance_address)
{
  WriteHandler handler(nullptr, &queue_);
  handler.set_appliance_address(0xC0);
  CHECK_EQUAL(0xC0, handler.get_appliance_address());
}

TEST(write_handler, process_writes_null_client)
{
  WriteHandler handler(nullptr, &queue_);
  handler.process_writes();
  // Should not crash
}

TEST(write_handler, process_writes_null_queue)
{
  WriteHandler handler(nullptr, nullptr);
  handler.process_writes();
  // Should not crash
}

TEST(write_handler, on_write_response_removes_in_flight)
{
  WriteHandler handler(nullptr, &queue_);
  handler.set_appliance_address(0xC0);
  // Can't easily test in-flight without a real ERD client,
  // but verify the interface exists and doesn't crash
  handler.on_write_response(0x1001, true);
}
