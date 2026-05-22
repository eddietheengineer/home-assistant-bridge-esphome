/*!
 * @file
 * @brief Tests for the GEA2 ERD client adapter.
 *
 * Validates that the adapter correctly delegates GEA3-style calls to the
 * underlying GEA2 client, rejects subscription operations, and re-publishes
 * GEA2 activity events through its GEA3-typed on_activity event.
 */

extern "C" {
#include "gea2_erd_client_adapter.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea2_erd_client_double.hpp"

TEST_GROUP(gea2_erd_client_adapter)
{
  enum {
    test_address = 0xC0,
    test_erd = 0x0001
  };

  gea2_erd_client_adapter_t adapter;
  tiny_gea2_erd_client_double_t gea2_client;

  void setup()
  {
    mock().strictOrder();
    tiny_gea2_erd_client_double_init(&gea2_client);
  }

  void teardown()
  {
    mock().clear();
  }

  void when_the_adapter_is_initialized()
  {
    gea2_erd_client_adapter_init(&adapter, &gea2_client.interface);
  }

  void given_that_the_adapter_has_been_initialized()
  {
    mock().disable();
    when_the_adapter_is_initialized();
    mock().enable();
  }

  void should_request_gea2_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&gea2_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  void should_request_gea2_write(uint8_t address, tiny_erd_t erd,
                                  const void* data, uint8_t data_size)
  {
    mock()
      .expectOneCall("write")
      .onObject(&gea2_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("data",
        reinterpret_cast<const unsigned char*>(data), data_size)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  void trigger_gea2_read_completed(uint8_t address, tiny_erd_t erd,
                                    const void* data, uint8_t data_size)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.request_id = 1;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }

  void trigger_gea2_read_failed(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_read_failed;
    args.address = address;
    args.read_failed.request_id = 1;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea2_erd_client_read_failure_reason_retries_exhausted;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }

  void trigger_gea2_write_completed(uint8_t address, tiny_erd_t erd,
                                     const void* data, uint8_t data_size)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_write_completed;
    args.address = address;
    args.write_completed.request_id = 1;
    args.write_completed.erd = erd;
    args.write_completed.data = data;
    args.write_completed.data_size = data_size;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }

  void trigger_gea2_write_failed(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_write_failed;
    args.address = address;
    args.write_failed.request_id = 1;
    args.write_failed.erd = erd;
    args.write_failed.reason = tiny_gea2_erd_client_write_failure_reason_retries_exhausted;
    args.write_failed.data = nullptr;
    args.write_failed.data_size = 0;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }
};

/* ------------------------------------------------------------------ */
/* Read delegation                                                      */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, read_delegates_to_gea2_client)
{
  given_that_the_adapter_has_been_initialized();
  should_request_gea2_read(test_address, test_erd);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_read(
    &adapter.interface, &request_id, test_address, test_erd);

  CHECK_TRUE(result);
}

TEST(gea2_erd_client_adapter, read_returns_false_when_gea2_fails)
{
  given_that_the_adapter_has_been_initialized();

  mock()
    .expectOneCall("read")
    .onObject(&gea2_client)
    .ignoreOtherParameters()
    .andReturnValue(false);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_read(
    &adapter.interface, &request_id, test_address, test_erd);

  CHECK_FALSE(result);
}

/* ------------------------------------------------------------------ */
/* Write delegation                                                     */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, write_delegates_to_gea2_client)
{
  given_that_the_adapter_has_been_initialized();
  uint8_t test_data = 0x42;
  should_request_gea2_write(test_address, test_erd, &test_data, sizeof(test_data));

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_write(
    &adapter.interface, &request_id, test_address, test_erd,
    &test_data, sizeof(test_data));

  CHECK_TRUE(result);
}

TEST(gea2_erd_client_adapter, write_returns_false_when_gea2_fails)
{
  given_that_the_adapter_has_been_initialized();
  uint8_t test_data = 0x42;

  mock()
    .expectOneCall("write")
    .onObject(&gea2_client)
    .ignoreOtherParameters()
    .andReturnValue(false);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_write(
    &adapter.interface, &request_id, test_address, test_erd,
    &test_data, sizeof(test_data));

  CHECK_FALSE(result);
}

/* ------------------------------------------------------------------ */
/* Subscription rejection                                               */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, subscribe_returns_false)
{
  given_that_the_adapter_has_been_initialized();
  bool result = tiny_gea3_erd_client_subscribe(&adapter.interface, test_address);
  CHECK_FALSE(result);
}

TEST(gea2_erd_client_adapter, retain_subscription_returns_false)
{
  given_that_the_adapter_has_been_initialized();
  bool result = tiny_gea3_erd_client_retain_subscription(&adapter.interface, test_address);
  CHECK_FALSE(result);
}

/* ------------------------------------------------------------------ */
/* Activity event passthrough                                           */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, on_activity_returns_event_interface)
{
  given_that_the_adapter_has_been_initialized();
  i_tiny_event_t* event = tiny_gea3_erd_client_on_activity(&adapter.interface);
  CHECK(event != nullptr);
}

TEST(gea2_erd_client_adapter, gea2_read_completed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  uint8_t data = 0x05;

  /* Subscribe a mock listener to the adapter's on_activity event. */
  mock()
    .expectOneCall("tiny_event_publish")
    .withParameter("event_type",
      static_cast<unsigned>(tiny_gea3_erd_client_activity_type_read_completed));

  trigger_gea2_read_completed(test_address, test_erd, &data, sizeof(data));
}

TEST(gea2_erd_client_adapter, gea2_read_failed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  mock()
    .expectOneCall("tiny_event_publish")
    .withParameter("event_type",
      static_cast<unsigned>(tiny_gea3_erd_client_activity_type_read_failed));

  trigger_gea2_read_failed(test_address, test_erd);
}

TEST(gea2_erd_client_adapter, gea2_write_completed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  uint8_t data = 0x01;

  mock()
    .expectOneCall("tiny_event_publish")
    .withParameter("event_type",
      static_cast<unsigned>(tiny_gea3_erd_client_activity_type_write_completed));

  trigger_gea2_write_completed(test_address, test_erd, &data, sizeof(data));
}

TEST(gea2_erd_client_adapter, gea2_write_failed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  mock()
    .expectOneCall("tiny_event_publish")
    .withParameter("event_type",
      static_cast<unsigned>(tiny_gea3_erd_client_activity_type_write_failed));

  trigger_gea2_write_failed(test_address, test_erd);
}