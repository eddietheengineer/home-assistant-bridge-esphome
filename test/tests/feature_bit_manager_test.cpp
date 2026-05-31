/*!\
 * @file
 * @brief Unit tests for the self-driving FeatureBitManager class.
 *
 * Validates initialization, start(), event-driven ERD read sequencing (14 ERDs),
 * state transitions, failure handling, timer-driven incremental parsing, and
 * valid ERD list generation.
 *
 * The manager is fully self-driving: it subscribes to ERD client activity events
 * and uses a periodic timer for incremental parsing.  Tests trigger events via
 * tiny_gea3_erd_client_double_trigger_activity_event() and drive the parse timer
 * via tiny_timer_group_double_elapse_time().
 *
 * Note: Production headers with STL containers (<set>, <vector>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "feature_bit_manager.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"
#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

/* TEST_GROUP required for TEST(group, name) macro to work —
 * provides the base class that TEST-generated test classes inherit from. */
TEST_GROUP(feature_bit_manager)
{
  FeatureBitManager manager;
  tiny_gea3_erd_client_double_t erd_client;
  tiny_timer_group_double_t timer_group;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    tiny_gea3_erd_client_double_init(&erd_client);
    tiny_timer_group_double_init(&timer_group);
  }

  void teardown()
  {
    mock().clear();
  }

  void init_manager()
  {
    manager.init(&erd_client.interface, 0xC0, &timer_group.timer_group);
  }

  /* Expect that a read for the given ERD will be queued successfully. */
  void expect_successful_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client.interface)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  /* Expect that a read for the given ERD will fail to queue. */
  void expect_failed_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client.interface)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(false);
  }

  /* Trigger a read_completed activity event for the given ERD. */
  void trigger_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = 0xC0;
    args.read_completed.request_id = 0;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  /* Trigger a read_failed activity event for the given ERD. */
  void trigger_read_failed(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_not_supported;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  /* Full sequence: trigger all 14 ERD reads in order via activity events.
   * Each completed read triggers the manager to queue the next one internally. */
  void trigger_full_read_sequence()
  {
    uint8_t dummy_data[8] = {0};

    // 0x0008 -> 0x0001
    expect_successful_read(0xC0, ERD_MODEL_NUMBER);
    trigger_read_completed(ERD_APPLIANCE_TYPE, dummy_data, 1);

    // 0x0001 -> 0x0002
    expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
    trigger_read_completed(ERD_MODEL_NUMBER, dummy_data, 1);

    // 0x0002 -> 0x0092
    expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
    trigger_read_completed(ERD_SERIAL_NUMBER, dummy_data, 1);

    // 0x0092 -> 0x0093
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
    trigger_read_completed(ERD_COMMON_FEATURE_API, dummy_data, 8);

    // 0x0093 -> 0x0094
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, dummy_data, 8);

    // 0x0094 -> 0x0095
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, dummy_data, 8);

    // 0x0095 -> 0x0096
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, dummy_data, 8);

    // 0x0096 -> 0x0097
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, dummy_data, 8);

    // 0x0097 -> 0x0109
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, dummy_data, 8);

    // 0x0109 -> 0x010A
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, dummy_data, 8);

    // 0x010A -> 0x010B
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, dummy_data, 8);

    // 0x010B -> 0x010C
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, dummy_data, 8);

    // 0x010C -> 0x010D
    expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, dummy_data, 8);

    // 0x010D (last) -> transitions to PARSING, starts timer
    trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, dummy_data, 8);
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, init_stores_all_parameters)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);

  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  // Verify the manager can use the stored erd_client by calling start().
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("address", 0xC0)
    .withParameter("erd", ERD_APPLIANCE_TYPE)
    .ignoreOtherParameters()
    .andReturnValue(true);
  mgr.start();

  // State should remain READING_0008 after successful queue (self-driving model).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, mgr.get_state());
}

TEST(feature_bit_manager, init_sets_state_to_reading_0008)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);

  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, mgr.get_state());
}

TEST(feature_bit_manager, init_resets_parse_state)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);

  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  // State starts at READING_0008, not PARSING or COMPLETE.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, mgr.get_state());
}

/* ------------------------------------------------------------------ */
/* start() — begins the self-driving read sequence                      */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, start_queues_appliance_type_read_first)
{
  init_manager();
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);

  manager.start();

  // State remains READING_0008 (self-driving; no IN_FLIGHT state).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());
}

TEST(feature_bit_manager, start_is_idempotent)
{
  init_manager();

  // First call queues the read.
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  // Second call should be a no-op (state is still READING_0008, but the
  // read was already queued).  No additional read mock expected.
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());
}

TEST(feature_bit_manager, start_does_nothing_when_not_at_first_state)
{
  init_manager();

  // Advance to next state first.
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  // Now state is READING_0001. start() should be a no-op.
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Event-driven ERD read sequencing                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, read_completed_transitions_to_model_number)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

TEST(feature_bit_manager, read_completed_transitions_to_serial_number)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0002, manager.get_state());
}

TEST(feature_bit_manager, read_completed_transitions_to_common_feature_api)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0092, manager.get_state());
}

TEST(feature_bit_manager, read_completed_transitions_to_appliance_feature_api_0)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  uint8_t feature_data[8] = {0};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0093, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* read_failed() — skip behavior                                        */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, read_failed_skips_to_next_erd)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  // skip_to_next_erd_ calls queue_erd_read_ after setting the state,
  // so we must expect the next read BEFORE triggering the failure.
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_failed(ERD_APPLIANCE_TYPE);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

TEST(feature_bit_manager, read_failed_skips_model_to_serial)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_failed(ERD_MODEL_NUMBER);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0002, manager.get_state());
}

TEST(feature_bit_manager, read_failed_skips_serial_to_common_feature)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_failed(ERD_SERIAL_NUMBER);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0092, manager.get_state());
}

TEST(feature_bit_manager, read_failed_on_last_erd_transitions_to_parsing)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  uint8_t feature_data[8] = {0};

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, feature_data, 8);

  // Fail the last ERD (0x010D) — should transition to PARSING and start timer.
  // tiny_timer_start_periodic is a real C function (not mockable via interface),
  // so we just verify the state transition.
  trigger_read_failed(ERD_APPLIANCE_FEATURE_API_9);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Full read sequence -> PARSING state                                  */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, full_read_sequence_transitions_to_parsing)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  trigger_full_read_sequence();

  // After the last ERD (0x010D), state is PARSING and timer is started.
  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Timer-driven incremental parsing                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, incremental_parsing_completes_with_timer_elapse)
{
  init_manager();

  // Provide data with a known feature bit set (Primary = 0x00000001)
  // for ERD 0x0092, and non-zero data for appliance ERDs so they are
  // actually parsed (not skipped as empty).
  uint8_t data[1] = {0x06};

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);

  // Set common feature API with "Primary" bit (0x00000001) set.
  uint8_t common_data[8] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, common_data, 8);

  // Use non-zero appliance feature data so each ERD is actually parsed
  // (not skipped as empty). Format: [2B type][2B version][4B bitmap].
  // type=0x0001, version=1, bitmap=0x00000001 (non-zero so not skipped).
  uint8_t appliance_data[8] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, appliance_data, 8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, appliance_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse enough timer ticks to complete all parsing.
  // Common features: ceil(17 / COMMON_PARSE_PER_CALL) = ceil(17/4) = 5 ticks
  // Appliance ERDs: 10 ticks (one per ERD)
  // Finalization: 1 tick
  // Total: 16 ticks * PARSE_TICK_MS (5ms) = 80ms
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());
}

TEST(feature_bit_manager, parsing_state_is_reached_before_complete)
{
  init_manager();

  (void)0; /* data provided by event trigger */
  uint8_t data[8] = {0}; (void)data;

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  trigger_full_read_sequence();

  // State is PARSING, not yet COMPLETE.
  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse one tick — still parsing (common features take multiple ticks).
  tiny_timer_group_double_elapse_time(&timer_group, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Queue full behavior — stays in current state                         */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, queue_full_keeps_current_state)
{
  init_manager();

  // start() tries to queue a read but the queue is full.
  expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  // State remains READING_0008 when queue is full.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());

  // Elapse 50ms to trigger the retry timer, which attempts another read (also failing).
  expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
  tiny_timer_group_double_elapse_time(&timer_group, 50);

  // State still READING_0008 after the retry also fails.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Queue retry timer behavior                                           */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, queue_full_retry_timer_retries_on_elapse)
{
  init_manager();

  // start() tries to queue a read but the queue is full.
  expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());

  // Elapse 50ms to trigger the retry timer, which attempts another read (also failing).
  expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
  tiny_timer_group_double_elapse_time(&timer_group, 50);

  // State is still READING_0008 after the retry also fails.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());
}

TEST(feature_bit_manager, queue_retry_timer_noop_when_already_queued)
{
  init_manager();

  // start() tries to queue a read but the queue is full, arming the retry timer.
  expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());

  // An activity event arrives before the timer fires. Since read_queued_ is false,
  // on_erd_activity_ retries the queue and it succeeds this time. The event
  // itself is NOT processed (method returns early after queuing).
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  uint8_t data[1] = {0x06};
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  // Now read_queued_ is true. A second activity event processes the completion.
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  // Now read_queued_ is true (ERD_MODEL_NUMBER is in-flight).
  // Elapse 50ms — the retry timer fires but queue_retry_() sees
  // read_queued_ == true and does nothing.
  tiny_timer_group_double_elapse_time(&timer_group, 50);

  // No additional read mock needed — the timer was a no-op.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* ERD event filtering                                                  */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, unrelated_erd_events_are_ignored)
{
  init_manager();

  // start() queues a read for ERD_APPLIANCE_TYPE successfully.
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());

  // Trigger a read_completed for ERD_COMMON_FEATURE_API — wrong ERD,
  // we're waiting for ERD_APPLIANCE_TYPE. It should be silently ignored.
  uint8_t data[8] = {0};
  trigger_read_completed(ERD_COMMON_FEATURE_API, data, 8);

  // State should remain READING_0008; no additional read queued.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* get_valid_erds() — before/after parsing                              */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, get_valid_erds_returns_empty_before_complete)
{
  init_manager();

  const auto& erds = manager.get_valid_erds();

  CHECK_TRUE(erds.empty());
}

TEST(feature_bit_manager, get_valid_erds_returns_empty_during_parsing)
{
  init_manager();

  (void)0; /* data provided by event trigger */
  uint8_t data[8] = {0}; (void)data;

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  trigger_full_read_sequence();

  // In PARSING state, valid_erds is still empty until parsing completes.
  CHECK_TRUE(manager.get_valid_erds().empty());
}

TEST(feature_bit_manager, get_valid_erds_contains_mandatory_erds_after_full_parse)
{
  init_manager();

  uint8_t data[1] = {0x06};
  uint8_t feature_data[8] = {0};

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, feature_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, feature_data, 8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, feature_data, 8);

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  const auto& erds = manager.get_valid_erds();

  // Mandatory ERDs are always added after parsing completes.
  CHECK_TRUE(erds.count(ERD_MODEL_NUMBER) > 0);
  CHECK_TRUE(erds.count(ERD_SERIAL_NUMBER) > 0);
  CHECK_TRUE(erds.count(ERD_APPLIANCE_TYPE) > 0);
  CHECK_TRUE(erds.count(ERD_COMMON_FEATURE_API) > 0);
  CHECK_TRUE(erds.count(ERD_APPLIANCE_FEATURE_API_0) > 0);
  CHECK_TRUE(erds.count(ERD_APPLIANCE_FEATURE_API_9) > 0);
}

/* ------------------------------------------------------------------ */
/* read_completed with null data — skip behavior                        */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, read_completed_with_null_data_skips_erd)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, nullptr, 0);

  // Null data should skip to next ERD.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* get_valid_erds_vec() consistency                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, get_valid_erds_vec_matches_set_after_parse)
{
  init_manager();

  (void)0; /* data provided by event trigger */
  uint8_t data[8] = {0}; (void)data;

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  trigger_full_read_sequence();

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  const auto& set_erds = manager.get_valid_erds();
  const auto& vec_erds = manager.get_valid_erds_vec();

  CHECK_EQUAL(set_erds.size(), vec_erds.size());
}

/* ------------------------------------------------------------------ */
/* ERD data sizes capped at 8                                           */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, erd_data_sizes_capped_at_8)
{
  init_manager();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  uint8_t data[1] = {0x06};
  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);

  // Provide more than 8 bytes — should be capped internally.
  uint8_t big_data[16] = {0x00, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0xCC, 0xDD,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, big_data, 16);

  // The manager caps at 8 bytes internally.  Verify by checking that
  // the subsequent read was queued (proving the event was processed).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0093, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* State machine: each READING state maps to correct ERD                */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, each_reading_state_queues_correct_erd)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  tiny_timer_group_double_t tg;
  tiny_gea3_erd_client_double_init(&ec);
  tiny_timer_group_double_init(&tg);
  mgr.init(&ec.interface, 0xC0, &tg.timer_group);

  (void)0; /* data provided by event trigger */
  uint8_t data[8] = {0}; (void)data;

  // READING_0008 -> queues ERD_APPLIANCE_TYPE
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_APPLIANCE_TYPE)
    .ignoreOtherParameters()
    .andReturnValue(true);
  mgr.start();

  // READING_0001 -> queues ERD_MODEL_NUMBER
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_MODEL_NUMBER)
    .ignoreOtherParameters()
    .andReturnValue(true);
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_read_completed;
  args.address = 0xC0;
  args.read_completed.request_id = 0;
  args.read_completed.erd = ERD_APPLIANCE_TYPE;
  args.read_completed.data = data;
  args.read_completed.data_size = 1;
  tiny_gea3_erd_client_double_trigger_activity_event(&ec, &args);

  // READING_0002 -> queues ERD_SERIAL_NUMBER
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_SERIAL_NUMBER)
    .ignoreOtherParameters()
    .andReturnValue(true);
  args.read_completed.erd = ERD_MODEL_NUMBER;
  tiny_gea3_erd_client_double_trigger_activity_event(&ec, &args);

  // READING_0092 -> queues ERD_COMMON_FEATURE_API
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("erd", ERD_COMMON_FEATURE_API)
    .ignoreOtherParameters()
    .andReturnValue(true);
  args.read_completed.erd = ERD_SERIAL_NUMBER;
  tiny_gea3_erd_client_double_trigger_activity_event(&ec, &args);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0092, mgr.get_state());
}

/* ------------------------------------------------------------------ */
/* Parsing with feature bits set                                        */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, parsing_with_feature_bits_adds_erds_from_common)
{
  init_manager();

  uint8_t data[1] = {0x06};

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  trigger_read_completed(ERD_APPLIANCE_TYPE, data, 1);
  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  trigger_read_completed(ERD_MODEL_NUMBER, data, 1);
  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  trigger_read_completed(ERD_SERIAL_NUMBER, data, 1);

  // Common feature API with Primary bit (0x00000001) set.
  uint8_t common_data[8] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed(ERD_COMMON_FEATURE_API, common_data, 8);

  // All appliance ERDs with valid data.
  uint8_t appliance_data[8] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_0, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_1, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_2, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_3, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_4, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_5, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_6, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_7, appliance_data, 8);
  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_9);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_8, appliance_data, 8);
  trigger_read_completed(ERD_APPLIANCE_FEATURE_API_9, appliance_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());

  // The valid ERD set should include mandatory ERDs plus any from feature bits.
  const auto& erds = manager.get_valid_erds();
  CHECK_FALSE(erds.empty());
}

/* ------------------------------------------------------------------ */
/* Timer stop after parsing completes                                   */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, parse_timer_stops_after_complete)
{
  init_manager();

  (void)0; /* data provided by event trigger */
  uint8_t data[8] = {0}; (void)data;

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.start();

  trigger_full_read_sequence();

  CHECK_EQUAL(FEATURE_BIT_STATE_PARSING, manager.get_state());

  // Elapse timer to complete parsing (16 ticks * 5ms = 80ms).
  tiny_timer_group_double_elapse_time(&timer_group, 80);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());

  // Elapse more time — state should remain COMPLETE (timer was stopped).
  tiny_timer_group_double_elapse_time(&timer_group, 10);
  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, common_parse_per_call_is_4)
{
  CHECK_EQUAL(4u, FeatureBitManager::COMMON_PARSE_PER_CALL);
}

TEST(feature_bit_manager, parse_tick_ms_is_5)
{
  CHECK_EQUAL(5u, FeatureBitManager::PARSE_TICK_MS);
}

TEST(feature_bit_manager, queue_retry_ms_is_50)
{
  CHECK_EQUAL(50u, FeatureBitManager::QUEUE_RETRY_MS);
}
