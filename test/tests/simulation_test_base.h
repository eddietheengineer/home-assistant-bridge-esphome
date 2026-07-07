/*!
 * @file
 * @brief Shared base class for simulation-based unit tests.
 *
 * Provides common member declarations (timer_group, erd_client, test_cache),
 * common setup/teardown, and helper methods for time elapse and triggering
 * read events.
 */

#ifndef simulation_test_base_h
#define simulation_test_base_h

extern "C" {
#include "erd_cache.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

class simulation_test_base : public Utest
{
public:
  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  erd_cache_t test_cache;

  void simulation_test_base_setup()
  {
    mock().strictOrder();

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void simulation_test_base_teardown()
  {
    erd_cache_destroy(&test_cache);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void trigger_read_completed(uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed(tiny_erd_t erd,
    tiny_gea3_erd_client_read_failure_reason_t reason =
      tiny_gea3_erd_client_read_failure_reason_retries_exhausted)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = reason;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }
};

#endif
