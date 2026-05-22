/*!
 * @file
 * @brief Unit tests for the HeapMonitor class.
 *
 * Validates initialization, timing-based update gating, sensor publishing,
 * fragmentation calculations, and null-sensor handling.
 *
 * The run() method calls heap functions and publish_state() in this order:
 *   1. esp_get_free_heap_size() -> free_heap_sensor->publish_state()
 *   2. esp_get_minimum_free_heap_size() -> min_free_heap_sensor->publish_state()
 *   3. heap_caps_get_total_size() + heap_caps_get_free_size()
 *      -> fragmentation_sensor->publish_state()
 *
 * Mock expectations must match this interleaved call order when
 * mock().strictOrder() is used.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "heap_monitor.h"
#include "double/esphome_hal_double.hpp"
#include "double/sensor_mock.hpp"
#include "double/esp_heap_caps_double.hpp"
#include "esp_heap_caps.h"

using namespace esphome::geappliances_bridge;

/* TEST_GROUP required for TEST(group, name) macro to work —
 * provides the base class that TEST-generated test classes inherit from. */
TEST_GROUP(heap_monitor)
{
};

/* ------------------------------------------------------------------ */
/* Helpers: set expectations matching the actual call order in run()    */
/* ------------------------------------------------------------------ */

/* Expectations for run() when only free_heap sensor is non-null. */
static void expect_run_free_only(size_t free_heap, const char* name)
{
  mock().expectOneCall("esp_get_free_heap_size")
    .andReturnValue((unsigned long int)free_heap);
  mock().expectOneCall("sensor_publish_state")
    .withStringParameter("name", name)
    .withDoubleParameter("value", (double)(float)free_heap);
}

/* Expectations for run() when only fragmentation sensor is non-null. */
static void expect_run_frag_only(size_t total, size_t free, const char* name)
{
  float frag = (total > 0)
      ? (100.0f - (100.0f * (float)free / (float)total)) : 0.0f;
  mock().expectOneCall("heap_caps_get_total_size")
    .andReturnValue((unsigned long int)total);
  mock().expectOneCall("heap_caps_get_free_size")
    .andReturnValue((unsigned long int)free);
  mock().expectOneCall("sensor_publish_state")
    .withStringParameter("name", name)
    .withDoubleParameter("value", (double)frag);
}

/* Expectations for run() when all three sensors are non-null. */
static void expect_run_all(size_t free_heap, size_t min_free,
                           size_t total, size_t free_internal,
                           const char* fn, const char* mn, const char* frag_n)
{
  mock().expectOneCall("esp_get_free_heap_size")
    .andReturnValue((unsigned long int)free_heap);
  mock().expectOneCall("sensor_publish_state")
    .withStringParameter("name", fn)
    .withDoubleParameter("value", (double)(float)free_heap);

  mock().expectOneCall("esp_get_minimum_free_heap_size")
    .andReturnValue((unsigned long int)min_free);
  mock().expectOneCall("sensor_publish_state")
    .withStringParameter("name", mn)
    .withDoubleParameter("value", (double)(float)min_free);

  float frag = (total > 0)
      ? (100.0f - (100.0f * (float)free_internal / (float)total)) : 0.0f;
  mock().expectOneCall("heap_caps_get_total_size")
    .andReturnValue((unsigned long int)total);
  mock().expectOneCall("heap_caps_get_free_size")
    .andReturnValue((unsigned long int)free_internal);
  mock().expectOneCall("sensor_publish_state")
    .withStringParameter("name", frag_n)
    .withDoubleParameter("value", (double)frag);
}

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(heap_monitor, stores_all_sensor_pointers_and_interval)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};
  sensor_mock_t min_free_heap_sensor{"min_free_heap"};
  sensor_mock_t fragmentation_sensor{"fragmentation"};

  hm.init(&free_heap_sensor, &min_free_heap_sensor,
          &fragmentation_sensor, 5000);

  esphome_hal_double_set_millis(5000);
  expect_run_all(800000, 500000, 520000, 400000,
                 "free_heap", "min_free_heap", "fragmentation");
  hm.run();
}

TEST(heap_monitor, skips_null_sensors)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};

  hm.init(&free_heap_sensor, nullptr, nullptr, 1000);
  esphome_hal_double_set_millis(1000);
  expect_run_free_only(800000, "free_heap");
  hm.run();
}

TEST(heap_monitor, all_sensors_null_does_not_crash)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  hm.init(nullptr, nullptr, nullptr, 1000);
  esphome_hal_double_set_millis(1000);

  /* When all sensors are null, run() still calls the heap functions
   * (they are called before the null check in the production code).
   * Wait — actually no: in heap_monitor.cpp, each sensor block is:
   *   if (sensor != nullptr) { heap_call(); sensor->publish(); }
   * So if all sensors are null, NO heap functions are called at all. */
  hm.run();
}

TEST(heap_monitor, uses_default_interval_when_not_specified)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};
  sensor_mock_t min_free_heap_sensor{"min_free_heap"};
  sensor_mock_t fragmentation_sensor{"fragmentation"};

  hm.init(&free_heap_sensor, &min_free_heap_sensor, &fragmentation_sensor);

  /* Advance to just before the default interval — should NOT trigger. */
  esphome_hal_double_set_millis(59999);
  hm.run();

  /* Advance to the default interval — should trigger. */
  esphome_hal_double_set_millis(60000);
  expect_run_all(800000, 500000, 520000, 400000,
                 "free_heap", "min_free_heap", "fragmentation");
  hm.run();
}

/* ------------------------------------------------------------------ */
/* run() timing tests                                                   */
/* ------------------------------------------------------------------ */

TEST(heap_monitor, does_not_publish_when_not_enough_time_has_passed)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};

  hm.init(&free_heap_sensor, nullptr, nullptr, 2000);
  esphome_hal_double_set_millis(500);

  /* No mock expectations — run() should return early. */
  hm.run();
}

TEST(heap_monitor, publishes_when_exactly_interval_has_passed)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};

  hm.init(&free_heap_sensor, nullptr, nullptr, 1000);
  esphome_hal_double_set_millis(1000);
  expect_run_free_only(800000, "free_heap");
  hm.run();
}

TEST(heap_monitor, publishes_when_more_than_interval_has_passed)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};

  hm.init(&free_heap_sensor, nullptr, nullptr, 1000);
  esphome_hal_double_set_millis(1500);
  expect_run_free_only(800000, "free_heap");
  hm.run();
}

TEST(heap_monitor, updates_last_update_time_after_publish)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};

  hm.init(&free_heap_sensor, nullptr, nullptr, 1000);

  /* First run at t=1000. */
  esphome_hal_double_set_millis(1000);
  expect_run_free_only(800000, "free_heap");
  hm.run();

  /* At t=1500 — only 500 ms since last update, should NOT publish. */
  esphome_hal_double_set_millis(1500);
  hm.run();

  /* At t=2000 — 1000 ms since last update, should publish again. */
  esphome_hal_double_set_millis(2000);
  expect_run_free_only(700000, "free_heap");
  hm.run();
}

/* ------------------------------------------------------------------ */
/* Fragmentation calculation tests                                      */
/* ------------------------------------------------------------------ */

TEST(heap_monitor, fragmentation_calculation_is_correct)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t fragmentation_sensor{"fragmentation"};

  hm.init(nullptr, nullptr, &fragmentation_sensor, 1000);
  esphome_hal_double_set_millis(1000);
  expect_run_frag_only(520000, 400000, "fragmentation");
  hm.run();
}

TEST(heap_monitor, fragmentation_is_zero_when_total_heap_is_zero)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t fragmentation_sensor{"fragmentation"};

  hm.init(nullptr, nullptr, &fragmentation_sensor, 1000);
  esphome_hal_double_set_millis(1000);
  expect_run_frag_only(0, 0, "fragmentation");
  hm.run();
}

TEST(heap_monitor, fragmentation_is_zero_when_free_equals_total)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t fragmentation_sensor{"fragmentation"};

  hm.init(nullptr, nullptr, &fragmentation_sensor, 1000);
  esphome_hal_double_set_millis(1000);
  expect_run_frag_only(500000, 500000, "fragmentation");
  hm.run();
}

TEST(heap_monitor, fragmentation_is_100_when_free_is_zero)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t fragmentation_sensor{"fragmentation"};

  hm.init(nullptr, nullptr, &fragmentation_sensor, 1000);
  esphome_hal_double_set_millis(1000);
  expect_run_frag_only(500000, 0, "fragmentation");
  hm.run();
}

/* ------------------------------------------------------------------ */
/* Millis wrap-around test                                              */
/* ------------------------------------------------------------------ */

TEST(heap_monitor, handles_millis_wrap_around)
{
  esphome_hal_double_set_millis(0);
  mock().strictOrder();

  HeapMonitor hm;
  sensor_mock_t free_heap_sensor{"free_heap"};

  hm.init(&free_heap_sensor, nullptr, nullptr, 1000);

  /* First run near UINT32_MAX. */
  esphome_hal_double_set_millis(0xFFFFFFFF - 500);
  expect_run_free_only(800000, "free_heap");
  hm.run();

  /* Wrap around — should trigger. */
  esphome_hal_double_set_millis(0x500);
  expect_run_free_only(750000, "free_heap");
  hm.run();
}
