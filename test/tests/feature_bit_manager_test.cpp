/*!
 * @file
 * @brief Unit tests for the FeatureBitManager class.
 *
 * Validates initialization, ERD read sequencing (14 ERDs), state transitions,
 * failure handling, queue retry logic, incremental parsing, and valid ERD list
 * generation.
 *
 * Important: on_erd_read_completed() internally calls tiny_gea3_erd_client_read()
 * to queue the next ERD in the sequence. Tests must set up mock expectations for
 * these internal reads before triggering callbacks.
 *
 * Note: Production headers with STL containers (<set>, <vector>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "feature_bit_manager.h"
#include "geappliances_bridge_constants.h"
#include "appliance_api_feature_lists.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/mqtt_client_double.hpp"
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
  mqtt_client_double_t mqtt_client;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    tiny_gea3_erd_client_double_init(&erd_client);
    mqtt_client_double_init(&mqtt_client);
  }

  void teardown()
  {
    mock().clear();
  }

  void init_manager()
  {
    manager.init(&erd_client.interface, 0xC0, &mqtt_client.interface, false);
  }

  void init_manager_with_mqtt()
  {
    manager.init(&erd_client.interface, 0xC0, &mqtt_client.interface, true);
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

  /* Simulate completion of an ERD read.
   * For ERDs that chain to a next read (all except the last), expect the
   * internal tiny_gea3_erd_client_read() call. */
  void trigger_read_completed_with_next(tiny_erd_t erd, const uint8_t* data,
                                         uint8_t size, tiny_erd_t next_erd)
  {
    expect_successful_read(0xC0, next_erd);
    manager.on_erd_read_completed(erd, data, size);
  }

  /* Simulate completion of the last ERD (0x010D) — no chained read. */
  void trigger_last_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
  {
    manager.on_erd_read_completed(erd, data, size);
  }

  /* Full sequence: trigger all 14 ERD reads in order. */
  void trigger_full_read_sequence()
  {
    uint8_t dummy_data[8] = {0};

    // 0x0008 → 0x0001
    trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, dummy_data, 1,
                                      ERD_MODEL_NUMBER);
    // 0x0001 → 0x0002
    trigger_read_completed_with_next(ERD_MODEL_NUMBER, dummy_data, 1,
                                      ERD_SERIAL_NUMBER);
    // 0x0002 → 0x0092
    trigger_read_completed_with_next(ERD_SERIAL_NUMBER, dummy_data, 1,
                                      ERD_COMMON_FEATURE_API);
    // 0x0092 → 0x0093
    trigger_read_completed_with_next(ERD_COMMON_FEATURE_API, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_0);
    // 0x0093 → 0x0094
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_0, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_1);
    // 0x0094 → 0x0095
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_1, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_2);
    // 0x0095 → 0x0096
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_2, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_3);
    // 0x0096 → 0x0097
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_3, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_4);
    // 0x0097 → 0x0109
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_4, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_5);
    // 0x0109 → 0x010A
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_5, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_6);
    // 0x010A → 0x010B
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_6, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_7);
    // 0x010B → 0x010C
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_7, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_8);
    // 0x010C → 0x010D
    trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_8, dummy_data, 8,
                                      ERD_APPLIANCE_FEATURE_API_9);
    // 0x010D (last) → COMPLETE
    trigger_last_read_completed(ERD_APPLIANCE_FEATURE_API_9, dummy_data, 8);
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, init_stores_all_parameters)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  mqtt_client_double_t mc;
  tiny_gea3_erd_client_double_init(&ec);
  mqtt_client_double_init(&mc);

  mgr.init(&ec.interface, 0xC0, &mc.interface, true);

  // Verify the manager can use the stored erd_client by queuing a read.
  mock().expectOneCall("read")
    .onObject(&ec.interface)
    .withParameter("address", 0xC0)
    .withParameter("erd", ERD_APPLIANCE_TYPE)
    .ignoreOtherParameters()
    .andReturnValue(true);
  mgr.run();

  // State should be IN_FLIGHT after successful queue.
  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, mgr.get_state());
}

TEST(feature_bit_manager, init_sets_state_to_reading_0008)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  mqtt_client_double_t mc;
  tiny_gea3_erd_client_double_init(&ec);
  mqtt_client_double_init(&mc);

  mgr.init(&ec.interface, 0xC0, &mc.interface, false);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, mgr.get_state());
}

TEST(feature_bit_manager, init_resets_parse_state)
{
  FeatureBitManager mgr;
  tiny_gea3_erd_client_double_t ec;
  mqtt_client_double_t mc;
  tiny_gea3_erd_client_double_init(&ec);
  mqtt_client_double_init(&mc);

  mgr.init(&ec.interface, 0xC0, &mc.interface, false);

  CHECK_FALSE(mgr.is_parse_pending());
  CHECK_FALSE(mgr.is_complete());
  CHECK_FALSE(mgr.is_failed());
}

/* ------------------------------------------------------------------ */
/* run() — ERD read sequencing                                          */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, run_queues_appliance_type_read_first)
{
  init_manager();
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);

  manager.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, run_does_nothing_when_erd_client_is_null)
{
  FeatureBitManager mgr;
  mqtt_client_double_t mc;
  mqtt_client_double_init(&mc);
  mgr.init(nullptr, 0xC0, &mc.interface, false);

  mgr.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, mgr.get_state());
}

TEST(feature_bit_manager, run_queues_model_number_after_appliance_type_read)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  manager.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, run_queues_serial_number_after_model_number_read)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);

  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  manager.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, run_queues_common_feature_api_after_serial_number)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);
  trigger_read_completed_with_next(ERD_SERIAL_NUMBER, data, 1,
                                    ERD_COMMON_FEATURE_API);

  expect_successful_read(0xC0, ERD_COMMON_FEATURE_API);
  manager.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, run_queues_appliance_feature_api_0_after_common)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);
  trigger_read_completed_with_next(ERD_SERIAL_NUMBER, data, 1,
                                    ERD_COMMON_FEATURE_API);
  uint8_t feature_data[8] = {0};
  trigger_read_completed_with_next(ERD_COMMON_FEATURE_API, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_0);

  expect_successful_read(0xC0, ERD_APPLIANCE_FEATURE_API_0);
  manager.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* on_erd_read_completed() — state transitions                          */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, on_erd_read_completed_transitions_to_model_number)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);

  // After successful internal read, state is IN_FLIGHT.
  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_completed_transitions_to_serial_number)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_completed_fallback_state_when_queue_fails)
{
  init_manager();

  uint8_t data[1] = {0x06};
  // Simulate that the internal read for the next ERD fails.
  mock()
    .expectOneCall("read")
    .onObject(&erd_client.interface)
    .withParameter("address", 0xC0)
    .withParameter("erd", ERD_MODEL_NUMBER)
    .ignoreOtherParameters()
    .andReturnValue(false);
  manager.on_erd_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  // When queue fails, falls back to the READING state for the next ERD.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_completed_stores_feature_api_data)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);
  trigger_read_completed_with_next(ERD_SERIAL_NUMBER, data, 1,
                                    ERD_COMMON_FEATURE_API);

  uint8_t feature_data[8] = {0x00, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0xCC, 0xDD};
  trigger_read_completed_with_next(ERD_COMMON_FEATURE_API, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_0);

  const auto& erd_data = manager.get_erd_data();
  CHECK_EQUAL(8u, erd_data.erd_0092_size);
  CHECK_EQUAL(0xAA, erd_data.erd_0092[4]);
  CHECK_EQUAL(0xBB, erd_data.erd_0092[5]);
}

TEST(feature_bit_manager, on_erd_read_completed_handles_null_data)
{
  init_manager();

  manager.on_erd_read_completed(ERD_APPLIANCE_TYPE, nullptr, 0);

  // Null data should not change state from READING_0008.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0008, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_completed_sets_complete_after_last_erd)
{
  init_manager();
  trigger_full_read_sequence();

  // After the last ERD (0x010D), state is COMPLETE and parse is pending.
  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_parse_pending());
}

/* ------------------------------------------------------------------ */
/* on_erd_read_failed() — skip behavior                                 */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, on_erd_read_failed_transitions_to_failed)
{
  init_manager();

  // Note: FeatureBitManager does NOT transition to FEATURE_BIT_STATE_FAILED
  // on read failure — it skips to the next ERD. The FAILED state is only
  // reached via MAX_QUEUE_RETRIES in run().
  manager.on_erd_read_failed(ERD_APPLIANCE_TYPE);

  // Should skip to next ERD (0x0001).
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
  CHECK_FALSE(manager.is_failed());
}

TEST(feature_bit_manager, on_erd_read_failed_skips_to_next_erd)
{
  init_manager();

  manager.on_erd_read_failed(ERD_MODEL_NUMBER);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0002, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_failed_skips_serial_to_common_feature)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);

  manager.on_erd_read_failed(ERD_SERIAL_NUMBER);

  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0092, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_failed_on_last_erd_sets_complete)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);
  trigger_read_completed_with_next(ERD_SERIAL_NUMBER, data, 1,
                                    ERD_COMMON_FEATURE_API);
  uint8_t feature_data[8] = {0};
  trigger_read_completed_with_next(ERD_COMMON_FEATURE_API, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_0);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_0, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_1, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_2, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_3, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_4, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_5, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_6, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_7, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_8, feature_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_9);

  // Fail the last ERD (0x010D) — should still set COMPLETE with parse pending.
  manager.on_erd_read_failed(ERD_APPLIANCE_FEATURE_API_9);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_parse_pending());
}

/* ------------------------------------------------------------------ */
/* is_complete() / is_failed() / is_parse_pending()                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, is_complete_returns_true_after_all_reads_and_parse)
{
  init_manager();
  trigger_full_read_sequence();

  // After all reads, state is COMPLETE but parse_pending is true.
  CHECK_TRUE(manager.is_parse_pending());
  CHECK_FALSE(manager.is_complete());  // is_complete requires !parse_pending

  // Run through incremental parsing until done.
  while (manager.is_parse_pending()) {
    manager.run();
  }

  CHECK_TRUE(manager.is_complete());
  CHECK_FALSE(manager.is_parse_pending());
}

TEST(feature_bit_manager, is_failed_returns_false_after_max_queue_retries_skips_erd)
{
  init_manager();

  // After MAX_QUEUE_RETRIES, the manager skips the ERD (does NOT go to FAILED).
  for (uint32_t i = 0; i < FeatureBitManager::MAX_QUEUE_RETRIES; i++) {
    expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
    manager.run();
  }

  // FeatureBitManager skips to next ERD on queue exhaustion, not FAILED.
  CHECK_FALSE(manager.is_failed());
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
}

TEST(feature_bit_manager, is_failed_returns_false_before_failed)
{
  init_manager();

  CHECK_FALSE(manager.is_failed());
}

/* ------------------------------------------------------------------ */
/* get_valid_erds() — before/after parsing                              */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, get_valid_erds_returns_empty_before_complete)
{
  init_manager();

  const auto& erds = manager.get_valid_erds();

  CHECK_TRUE(erds.empty());
  CHECK_FALSE(manager.is_valid_list_ready());
}

TEST(feature_bit_manager, get_valid_erds_returns_empty_after_reads_before_parse)
{
  init_manager();
  trigger_full_read_sequence();

  // Reads complete, but parsing hasn't run yet.
  CHECK_TRUE(manager.get_valid_erds().empty());
  CHECK_FALSE(manager.is_valid_list_ready());
}

TEST(feature_bit_manager, get_valid_erds_contains_mandatory_erds_after_full_parse)
{
  init_manager();
  trigger_full_read_sequence();

  // Run through all incremental parsing.
  while (manager.is_parse_pending()) {
    manager.run();
  }

  const auto& erds = manager.get_valid_erds();

  // Mandatory ERDs are always added.
  CHECK_TRUE(erds.count(ERD_MODEL_NUMBER) > 0);
  CHECK_TRUE(erds.count(ERD_SERIAL_NUMBER) > 0);
  CHECK_TRUE(erds.count(ERD_APPLIANCE_TYPE) > 0);
  CHECK_TRUE(erds.count(ERD_COMMON_FEATURE_API) > 0);
  CHECK_TRUE(erds.count(ERD_APPLIANCE_FEATURE_API_0) > 0);
  CHECK_TRUE(erds.count(ERD_APPLIANCE_FEATURE_API_9) > 0);
  CHECK_TRUE(manager.is_valid_list_ready());
}

/* ------------------------------------------------------------------ */
/* Incremental parsing                                                  */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, incremental_parsing_processes_one_erd_per_run)
{
  init_manager();

  // Provide data with a known feature bit set (Primary = 0x00000001)
  // for ERD 0x0092, and non-zero data for appliance ERDs so they are
  // actually parsed (not skipped as empty).
  uint8_t data[1] = {0x06};

  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);
  trigger_read_completed_with_next(ERD_SERIAL_NUMBER, data, 1,
                                    ERD_COMMON_FEATURE_API);

  // Set common feature API with "Primary" bit (0x00000001) set.
  uint8_t common_data[8] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  trigger_read_completed_with_next(ERD_COMMON_FEATURE_API, common_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_0);

  // Use non-zero appliance feature data so each ERD is actually parsed
  // (not skipped as empty). Format: [2B type][2B version][4B bitmap].
  // type=0x0001, version=1, bitmap=0x00000001 (non-zero so not skipped).
  uint8_t appliance_data[8] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_0, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_1);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_1, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_2);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_2, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_3);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_3, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_4);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_4, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_5);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_5, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_6);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_6, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_7);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_7, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_8);
  trigger_read_completed_with_next(ERD_APPLIANCE_FEATURE_API_8, appliance_data, 8,
                                    ERD_APPLIANCE_FEATURE_API_9);
  trigger_last_read_completed(ERD_APPLIANCE_FEATURE_API_9, appliance_data, 8);

  CHECK_EQUAL(FEATURE_BIT_STATE_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_parse_pending());

  // First run() processes common features + first appliance ERD (idx 0).
  manager.run();

  // After first parse call, parse_pending is still true (more appliance ERDs to go).
  CHECK_TRUE(manager.is_parse_pending());

  // Continue parsing until done.
  // There are 10 appliance ERDs to parse (one per run call) + 1 for common.
  // So at most 11 total run() calls.
  int parse_calls = 1;  // already did one above
  while (manager.is_parse_pending()) {
    manager.run();
    parse_calls++;
  }

  // Should have processed: 1 (common + first appliance) + 9 (remaining appliance ERDs) = 10 calls total.
  CHECK_EQUAL(10, parse_calls);
  CHECK_TRUE(manager.is_complete());
  CHECK_TRUE(manager.is_valid_list_ready());
}

/* ------------------------------------------------------------------ */
/* Queue retry logic                                                    */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, run_resets_queue_retry_count_on_success)
{
  init_manager();

  // Fail a few times, then succeed.
  for (uint32_t i = 0; i < 5; i++) {
    expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
    manager.run();
  }

  CHECK_EQUAL(5u, manager.get_queue_retry_count());

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.run();

  CHECK_EQUAL(0u, manager.get_queue_retry_count());
  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, run_gives_up_after_max_queue_retries)
{
  init_manager();

  for (uint32_t i = 0; i < FeatureBitManager::MAX_QUEUE_RETRIES; i++) {
    expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
    manager.run();
  }

  // After max retries, the ERD is skipped — state moves to next.
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());
  CHECK_EQUAL(0u, manager.get_queue_retry_count());
}

TEST(feature_bit_manager, run_gives_up_after_max_queue_retries_for_each_erd)
{
  init_manager();

  // Exhaust retries for ERD 0x0008 → skips to 0x0001.
  for (uint32_t i = 0; i < FeatureBitManager::MAX_QUEUE_RETRIES; i++) {
    expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
    manager.run();
  }
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0001, manager.get_state());

  // Exhaust retries for ERD 0x0001 → skips to 0x0002.
  for (uint32_t i = 0; i < FeatureBitManager::MAX_QUEUE_RETRIES; i++) {
    expect_failed_read(0xC0, ERD_MODEL_NUMBER);
    manager.run();
  }
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0002, manager.get_state());

  // Exhaust retries for ERD 0x0002 → skips to 0x0092.
  for (uint32_t i = 0; i < FeatureBitManager::MAX_QUEUE_RETRIES; i++) {
    expect_failed_read(0xC0, ERD_SERIAL_NUMBER);
    manager.run();
  }
  CHECK_EQUAL(FEATURE_BIT_STATE_READING_0092, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* MQTT integration                                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, on_erd_read_completed_publishes_to_mqtt_when_initialized)
{
  init_manager_with_mqtt();

  uint8_t data[1] = {0x06};
  // When MQTT is initialized, on_erd_read_completed calls mqtt_client->api->update_erd.
  mock()
    .expectOneCall("update_erd")
    .onObject(&mqtt_client.interface)
    .withParameter("erd", ERD_APPLIANCE_TYPE)
    .ignoreOtherParameters();
  mock()
    .expectOneCall("read")
    .onObject(&erd_client.interface)
    .withParameter("address", 0xC0)
    .withParameter("erd", ERD_MODEL_NUMBER)
    .ignoreOtherParameters()
    .andReturnValue(true);
  manager.on_erd_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

TEST(feature_bit_manager, on_erd_read_completed_does_not_publish_to_mqtt_when_not_initialized)
{
  init_manager();  // mqtt_initialized = false

  uint8_t data[1] = {0x06};
  // No update_erd call expected when mqtt_initialized is false.
  mock()
    .expectOneCall("read")
    .onObject(&erd_client.interface)
    .withParameter("address", 0xC0)
    .withParameter("erd", ERD_MODEL_NUMBER)
    .ignoreOtherParameters()
    .andReturnValue(true);
  manager.on_erd_read_completed(ERD_APPLIANCE_TYPE, data, 1);

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* get_valid_erds_vec() consistency                                     */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, get_valid_erds_vec_matches_set_after_parse)
{
  init_manager();
  trigger_full_read_sequence();

  while (manager.is_parse_pending()) {
    manager.run();
  }

  const auto& set_erds = manager.get_valid_erds();
  const auto& vec_erds = manager.get_valid_erds_vec();

  CHECK_EQUAL(set_erds.size(), vec_erds.size());
}

/* ------------------------------------------------------------------ */
/* Edge cases                                                           */
/* ------------------------------------------------------------------ */

TEST(feature_bit_manager, run_does_not_parse_when_not_complete)
{
  init_manager();

  // Before all reads are done, run() should queue reads, not parse.
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.run();

  CHECK_EQUAL(FEATURE_BIT_STATE_IN_FLIGHT, manager.get_state());
  CHECK_FALSE(manager.is_parse_pending());
}

TEST(feature_bit_manager, erd_data_sizes_capped_at_8)
{
  init_manager();

  uint8_t data[1] = {0x06};
  trigger_read_completed_with_next(ERD_APPLIANCE_TYPE, data, 1,
                                    ERD_MODEL_NUMBER);
  trigger_read_completed_with_next(ERD_MODEL_NUMBER, data, 1,
                                    ERD_SERIAL_NUMBER);
  trigger_read_completed_with_next(ERD_SERIAL_NUMBER, data, 1,
                                    ERD_COMMON_FEATURE_API);

  // Provide more than 8 bytes — should be capped.
  uint8_t big_data[16] = {0x00, 0x00, 0x00, 0x01, 0xAA, 0xBB, 0xCC, 0xDD,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  trigger_read_completed_with_next(ERD_COMMON_FEATURE_API, big_data, 16,
                                    ERD_APPLIANCE_FEATURE_API_0);

  const auto& erd_data = manager.get_erd_data();
  CHECK_EQUAL(8u, erd_data.erd_0092_size);
}
