/*
 * @file
 * @brief Unit tests for the DeviceIdentityManager class.
 *
 * Validates initialization, ERD read sequencing, device ID generation,
 * indefinite retry logic, queue retry behavior, and configured ID precedence.
 *
 * Each identity ERD (0x0008, 0x0001, 0x0002) is retried indefinitely on
 * failure -- the manager never moves on until it has successfully read all
 * three. There are no fallback values.
 *
 * Important: on_erd_read_completed() for appliance type and model number
 * internally calls tiny_gea3_erd_client_read() to queue the next ERD.
 * Tests must set up mock expectations for these internal reads.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "device_identity_manager.h"
#include "geappliances_bridge_constants.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include <cstring>

using namespace esphome::geappliances_bridge;

/* TEST_GROUP required for TEST(group, name) macro to work --
 * provides the base class that TEST-generated test classes inherit from. */
TEST_GROUP(device_identity_manager)
{
  DeviceIdentityManager manager;
  tiny_gea3_erd_client_double_t erd_client;

  void setup()
  {
    mock().clear();
    mock().strictOrder();
    tiny_gea3_erd_client_double_init(&erd_client);
  }

  void teardown()
  {
    mock().clear();
  }

  void init_with_configured_id(const std::string& id)
  {
    manager.init(id, &erd_client.interface, 0xC0);
  }

  void init_without_configured_id()
  {
    manager.init("", &erd_client.interface, 0xC0);
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

  /* Simulate appliance type read completion.
   * This internally queues a model number read -- expect it. */
  void trigger_appliance_type_read_completed(const void* data, uint8_t size)
  {
    expect_successful_read(0xC0, ERD_MODEL_NUMBER);
    manager.on_erd_read_completed(ERD_APPLIANCE_TYPE,
                                  static_cast<const uint8_t*>(data), size);
  }

  /* Simulate model number read completion.
   * This internally queues a serial number read -- expect it. */
  void trigger_model_number_read_completed(const void* data, uint8_t size)
  {
    expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
    manager.on_erd_read_completed(ERD_MODEL_NUMBER,
                                  static_cast<const uint8_t*>(data), size);
  }

  /* Simulate serial number read completion.
   * This does NOT queue another read -- goes straight to COMPLETE. */
  void trigger_serial_number_read_completed(const void* data, uint8_t size)
  {
    manager.on_erd_read_completed(ERD_SERIAL_NUMBER,
                                  static_cast<const uint8_t*>(data), size);
  }

  /* Simulate an ERD read failure. With indefinite retry, this simply
   * stays in the current READING_* state so run() will retry. */
  void trigger_read_failed(tiny_erd_t erd)
  {
    manager.on_erd_read_failed(erd);
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, init_stores_configured_id_and_uses_it_immediately)
{
  init_with_configured_id("my-custom-device");

  CHECK_EQUAL(DEVICE_ID_STATE_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_complete());
  CHECK_FALSE(manager.is_failed());
  CHECK(strcmp(manager.get_device_id().c_str(), "my-custom-device") == 0);
}

TEST(device_identity_manager, init_without_configured_id_starts_reading_appliance_type)
{
  init_without_configured_id();

  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
  CHECK_FALSE(manager.is_complete());
  CHECK_FALSE(manager.is_failed());
}

TEST(device_identity_manager, init_stores_host_address)
{
  init_without_configured_id();
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);

  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* run() -- ERD read sequencing                                         */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, run_queues_appliance_type_read_first)
{
  init_without_configured_id();
  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);

  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

TEST(device_identity_manager, run_queues_model_number_after_appliance_type_read)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

TEST(device_identity_manager, run_queues_serial_number_after_model_number_read)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("XYZ123", 6);

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

TEST(device_identity_manager, run_does_nothing_when_complete)
{
  init_with_configured_id("test-device");

  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_COMPLETE, manager.get_state());
}

TEST(device_identity_manager, run_does_nothing_when_erd_client_is_null)
{
  manager.init("", nullptr, 0xC0);

  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* on_erd_read_completed() -- processing each ERD                       */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, on_erd_read_completed_processes_appliance_type)
{
  init_without_configured_id();

  uint8_t data[] = {0x03};
  trigger_appliance_type_read_completed(data, 1);

  CHECK_EQUAL(0x03, manager.get_appliance_type());
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

TEST(device_identity_manager, on_erd_read_completed_processes_model_number)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("GTCH2230", 8);

  CHECK(strcmp(manager.get_model_number().c_str(), "GTCH2230") == 0);
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

TEST(device_identity_manager, on_erd_read_completed_processes_serial_number_and_generates_id)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("GTCH2230", 8);
  trigger_serial_number_read_completed("SN123456", 8);

  CHECK(strcmp(manager.get_serial_number().c_str(), "SN123456") == 0);
  CHECK_TRUE(manager.is_complete());
  CHECK_EQUAL(DEVICE_ID_STATE_COMPLETE, manager.get_state());
  CHECK(strcmp(manager.get_device_id().c_str(), "Dishwasher_GTCH2230_SN123456") == 0);
}

TEST(device_identity_manager, on_erd_read_completed_trims_trailing_null_bytes)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);

  uint8_t model_data[] = "ABC\0\0\0";
  trigger_model_number_read_completed(model_data, 6);

  CHECK(strcmp(manager.get_model_number().c_str(), "ABC") == 0);
}

TEST(device_identity_manager, on_erd_read_completed_handles_empty_appliance_type_data)
{
  init_without_configured_id();

  manager.on_erd_read_completed(ERD_APPLIANCE_TYPE, nullptr, 0);

  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* on_erd_read_failed() -- indefinite retry logic                       */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, on_erd_read_failed_retries_appliance_type_read)
{
  init_without_configured_id();

  trigger_read_failed(ERD_APPLIANCE_TYPE);

  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
}

TEST(device_identity_manager, on_erd_read_failed_retries_model_number_read)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);

  trigger_read_failed(ERD_MODEL_NUMBER);

  CHECK_EQUAL(DEVICE_ID_STATE_READING_MODEL_NUMBER, manager.get_state());
}

TEST(device_identity_manager, on_erd_read_failed_retries_serial_number_read)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);

  trigger_read_failed(ERD_SERIAL_NUMBER);

  CHECK_EQUAL(DEVICE_ID_STATE_READING_SERIAL_NUMBER, manager.get_state());
}

TEST(device_identity_manager, on_erd_read_failed_never_uses_fallback)
{
  // Even after many failures, the manager stays in the reading state
  // and never produces a fallback device ID.
  init_without_configured_id();

  for (int i = 0; i < 10; i++) {
    trigger_read_failed(ERD_APPLIANCE_TYPE);
  }

  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
  CHECK_FALSE(manager.is_complete());
  CHECK_FALSE(manager.is_failed());
  CHECK(strcmp(manager.get_device_id().c_str(), "") == 0);
}

/* ------------------------------------------------------------------ */
/* Queue retry logic -- indefinite retry, never gives up                */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, run_retries_indefinitely_on_queue_full)
{
  init_without_configured_id();

  // Even after many queue failures, the manager stays in the reading state.
  for (uint32_t i = 0; i < 100; i++) {
    expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
    manager.run();
  }

  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
  CHECK_FALSE(manager.is_failed());
  CHECK_FALSE(manager.is_complete());
}

TEST(device_identity_manager, run_resets_queue_retry_count_on_success)
{
  init_without_configured_id();

  for (uint32_t i = 0; i < 5; i++) {
    expect_failed_read(0xC0, ERD_APPLIANCE_TYPE);
    manager.run();
  }

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);

  for (uint32_t i = 0; i < 3; i++) {
    expect_failed_read(0xC0, ERD_MODEL_NUMBER);
    manager.run();
  }

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  manager.run();

  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Configured device ID precedence                                      */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, configured_device_id_takes_precedence_over_generated)
{
  init_with_configured_id("my-custom-device");

  CHECK(strcmp(manager.get_device_id().c_str(), "my-custom-device") == 0);
  CHECK_TRUE(manager.is_complete());
}

TEST(device_identity_manager, configured_id_empty_string_is_treated_as_not_configured)
{
  manager.init("", &erd_client.interface, 0xC0);

  CHECK_FALSE(manager.is_complete());
  CHECK_EQUAL(DEVICE_ID_STATE_READING_APPLIANCE_TYPE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Device ID format and sanitization                                    */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, device_id_sanitizes_special_characters_in_serial_number)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);

  uint8_t serial_data[] = "SN+123#456/7";
  trigger_serial_number_read_completed(serial_data, sizeof(serial_data) - 1);

  CHECK(strcmp(manager.get_device_id().c_str(), "Dishwasher_ABC_SN_123_456_7") == 0);
}

TEST(device_identity_manager, device_id_sanitizes_spaces_in_serial_number)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);

  uint8_t serial_data[] = "A B C";
  trigger_serial_number_read_completed(serial_data, 5);

  CHECK(strcmp(manager.get_device_id().c_str(), "Dishwasher_ABC_A_B_C") == 0);
}

TEST(device_identity_manager, generated_device_id_matches_final_when_no_configured_id)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);
  trigger_serial_number_read_completed("SN123", 5);

  CHECK(strcmp(manager.get_device_id().c_str(),
                manager.get_generated_device_id().c_str()) == 0);
}

/* ------------------------------------------------------------------ */
/* Full happy path integration test                                     */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, full_happy_path_from_init_to_complete)
{
  init_without_configured_id();

  expect_successful_read(0xC0, ERD_APPLIANCE_TYPE);
  manager.run();
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  uint8_t at_data[] = {0x04};
  trigger_appliance_type_read_completed(at_data, 1);
  CHECK_EQUAL(0x04, manager.get_appliance_type());
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  expect_successful_read(0xC0, ERD_MODEL_NUMBER);
  manager.run();
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  uint8_t model_data[] = "GFW12345";
  trigger_model_number_read_completed(model_data, 8);
  CHECK(strcmp(manager.get_model_number().c_str(), "GFW12345") == 0);
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  expect_successful_read(0xC0, ERD_SERIAL_NUMBER);
  manager.run();
  CHECK_EQUAL(DEVICE_ID_STATE_IDLE, manager.get_state());

  uint8_t serial_data[] = "WH20230001";
  trigger_serial_number_read_completed(serial_data, 10);

  CHECK_TRUE(manager.is_complete());
  CHECK_EQUAL(DEVICE_ID_STATE_COMPLETE, manager.get_state());
  CHECK(strcmp(manager.get_device_id().c_str(), "Microwave_GFW12345_WH20230001") == 0);
  CHECK(strcmp(manager.get_model_number().c_str(), "GFW12345") == 0);
  CHECK(strcmp(manager.get_serial_number().c_str(), "WH20230001") == 0);
}

/* ------------------------------------------------------------------ */
/* Edge cases                                                           */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, is_failed_returns_false_before_failure)
{
  init_without_configured_id();
  CHECK_FALSE(manager.is_failed());
}

TEST(device_identity_manager, is_complete_returns_false_before_completion)
{
  init_without_configured_id();
  CHECK_FALSE(manager.is_complete());
}

TEST(device_identity_manager, get_device_id_returns_empty_before_completion)
{
  init_without_configured_id();
  CHECK(strcmp(manager.get_device_id().c_str(), "") == 0);
}

TEST(device_identity_manager, appliance_type_unknown_for_value_0)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x00};
  trigger_appliance_type_read_completed(at_data, 1);

  CHECK_EQUAL(0x00, manager.get_appliance_type());
}

TEST(device_identity_manager, appliance_type_unknown_for_large_value)
{
  init_without_configured_id();

  uint8_t at_data[] = {0xFF};
  trigger_appliance_type_read_completed(at_data, 1);

  CHECK_EQUAL(0xFF, manager.get_appliance_type());
}

/* ------------------------------------------------------------------ */
/* Sanitization edge cases                                              */
/* ------------------------------------------------------------------ */

TEST(device_identity_manager, sanitize_replaces_dollar_sign)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);

  uint8_t serial_data[] = "$SN$";
  trigger_serial_number_read_completed(serial_data, sizeof(serial_data) - 1);

  CHECK(strcmp(manager.get_device_id().c_str(), "Dishwasher_ABC__SN_") == 0);
}

TEST(device_identity_manager, sanitize_handles_control_characters)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);

  uint8_t serial_data[] = {0x01, 'S', 'N', 0x1F};
  trigger_serial_number_read_completed(serial_data, 4);

  CHECK(strcmp(manager.get_device_id().c_str(), "Dishwasher_ABC__SN_") == 0);
}

TEST(device_identity_manager, sanitize_handles_high_bytes_above_0x7E)
{
  init_without_configured_id();

  uint8_t at_data[] = {0x06};
  trigger_appliance_type_read_completed(at_data, 1);
  trigger_model_number_read_completed("ABC", 3);

  uint8_t serial_data[] = {0x7F, 'S', 'N', 0xFF};
  trigger_serial_number_read_completed(serial_data, 4);

  CHECK(strcmp(manager.get_device_id().c_str(), "Dishwasher_ABC__SN_") == 0);
}
