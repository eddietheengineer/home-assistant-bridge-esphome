/*!
 * @file
 * @brief Unit tests for the AutodiscoveryManager class.
 *
 * Validates initialization, state machine transitions, broadcast handling,
 * retry logic, and completion behavior.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "autodiscovery_manager.h"
#include "double/esphome_hal_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_gea2_erd_client_double.hpp"

#include "geappliances_bridge_constants.h"

using namespace esphome::geappliances_bridge;

/* TEST_GROUP required for TEST(group, name) macro to work. */
TEST_GROUP(autodiscovery_manager)
{
  AutodiscoveryManager manager;
  tiny_gea3_erd_client_double_t gea3_client;
  tiny_gea2_erd_client_double_t gea2_client;
  tiny_gea3_erd_client_double_t gea2_adapter;
  bool callback_called;

  void setup()
  {
    mock().strictOrder();
    tiny_gea3_erd_client_double_init(&gea3_client);
    tiny_gea2_erd_client_double_init(&gea2_client);
    tiny_gea3_erd_client_double_init(&gea2_adapter);
    callback_called = false;
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    mock().clear();
  }

  void init_both_uart()
  {
    manager.init(&gea3_client.interface,
                 &gea2_client.interface,
                 &gea2_adapter.interface,
                 true,  /* has_gea3_uart */
                 true,  /* has_gea2_uart */
                 [this]() { callback_called = true; });
  }

  void init_gea3_only()
  {
    manager.init(&gea3_client.interface,
                 nullptr,
                 nullptr,
                 true,   /* has_gea3_uart */
                 false,  /* has_gea2_uart */
                 [this]() { callback_called = true; });
  }

  void init_gea2_only()
  {
    manager.init(nullptr,
                 &gea2_client.interface,
                 nullptr,
                 false,  /* has_gea3_uart */
                 true,   /* has_gea2_uart */
                 [this]() { callback_called = true; });
  }

  /* Expect the GEA3 client to accept a broadcast read. */
  void expect_gea3_broadcast_read()
  {
    mock().expectOneCall("read")
      .onObject(&gea3_client.interface)
      .withParameter("address", GEA_BROADCAST_ADDRESS)
      .withParameter("erd", ERD_APPLIANCE_TYPE)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  /* Expect the GEA2 client to accept a broadcast read. */
  void expect_gea2_broadcast_read()
  {
    mock().expectOneCall("read")
      .onObject(&gea2_client.interface)
      .withParameter("address", GEA_BROADCAST_ADDRESS)
      .withParameter("erd", ERD_APPLIANCE_TYPE)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }
};

/* ------------------------------------------------------------------ */
/* Initialization                                                       */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, init_sets_state_to_waiting_5s)
{
  init_both_uart();
  CHECK_EQUAL(AUTODISCOVERY_WAITING_5S, manager.get_state());
}

TEST(autodiscovery_manager, init_stores_host_address_as_zero)
{
  init_both_uart();
  CHECK_EQUAL(0, manager.get_host_address());
}

TEST(autodiscovery_manager, init_stores_active_erd_client_as_null)
{
  init_both_uart();
  CHECK(manager.get_active_erd_client() == nullptr);
}

TEST(autodiscovery_manager, init_sets_retry_count_to_zero)
{
  init_both_uart();
  CHECK_EQUAL(0, manager.get_retry_count());
}

TEST(autodiscovery_manager, init_is_not_complete)
{
  init_both_uart();
  CHECK_FALSE(manager.is_complete());
  CHECK_FALSE(manager.is_failed());  // is_failed() always returns false — retries indefinitely
}

TEST(autodiscovery_manager, is_failed_always_returns_false)
{
  init_both_uart();
  CHECK_FALSE(manager.is_failed());

  /* Even after many timeouts with no response, is_failed() stays false */
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t gea3_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(gea3_time);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* Timeout with no response → retry GEA2 */
  esphome_hal_double_set_millis(gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();  /* → GEA2_BROADCAST_PENDING */
  expect_gea2_broadcast_read();
  uint32_t gea2_time = gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100;
  esphome_hal_double_set_millis(gea2_time);
  manager.run();  /* → GEA2_BROADCAST_WAITING */

  /* Timeout again with no response → retry GEA3 */
  esphome_hal_double_set_millis(gea2_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t gea3_time2 = gea2_time + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100;
  esphome_hal_double_set_millis(gea3_time2);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  CHECK_FALSE(manager.is_failed());
  CHECK_FALSE(manager.is_complete());
  /* Should still be in a retrying state, not COMPLETE */
  CHECK(manager.get_state() != AUTODISCOVERY_COMPLETE);
}

/* ------------------------------------------------------------------ */
/* WAITING_5S → GEA3/GEA2 BROADCAST_PENDING                           */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, run_transitions_to_gea3_broadcast_after_5s)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();
  CHECK_EQUAL(AUTODISCOVERY_GEA3_BROADCAST_PENDING, manager.get_state());
}

TEST(autodiscovery_manager, run_does_not_transition_before_5s)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS - 1);
  manager.run();
  CHECK_EQUAL(AUTODISCOVERY_WAITING_5S, manager.get_state());
}

TEST(autodiscovery_manager, run_transitions_to_gea2_if_no_gea3_uart)
{
  init_gea2_only();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();
  CHECK_EQUAL(AUTODISCOVERY_GEA2_BROADCAST_PENDING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* GEA3 BROADCAST PENDING → WAITING                                     */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, gea3_pending_sends_broadcast_and_waits)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  manager.run();  /* → GEA3_BROADCAST_WAITING */
  CHECK_EQUAL(AUTODISCOVERY_GEA3_BROADCAST_WAITING, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* on_broadcast_response handling                                       */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, on_broadcast_response_sets_gea3_discovery)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  manager.on_broadcast_response(0xB8, 0x03, true);
  CHECK_EQUAL(0xB8, manager.get_host_address());
  CHECK_EQUAL(&gea3_client.interface, manager.get_active_erd_client());
  CHECK_FALSE(manager.is_gea2_protocol());
}

TEST(autodiscovery_manager, on_broadcast_response_sets_gea2_discovery)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t gea3_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(gea3_time);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* GEA3 times out with no response → GEA2 */
  esphome_hal_double_set_millis(gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();  /* → GEA2_BROADCAST_PENDING */
  expect_gea2_broadcast_read();
  uint32_t gea2_time = gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100;
  esphome_hal_double_set_millis(gea2_time);
  manager.run();  /* → GEA2_BROADCAST_WAITING */

  manager.on_broadcast_response(0xB8, 0x03, false);
  CHECK_EQUAL(0xB8, manager.get_host_address());
  CHECK_EQUAL(&gea2_adapter.interface, manager.get_active_erd_client());
  /* gea2_protocol_active_ is set in run() on completion, not in on_broadcast_response() */
  CHECK_FALSE(manager.is_gea2_protocol());
}

TEST(autodiscovery_manager, on_broadcast_response_ignores_wrong_protocol)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS + 100);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* GEA2 response during GEA3 window is ignored */
  manager.on_broadcast_response(0xB8, 0x03, false);
  CHECK(manager.get_active_erd_client() == nullptr);
}

/* ------------------------------------------------------------------ */
/* Completion flow                                                        */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, gea3_discovery_completes_on_timeout)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t broadcast_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(broadcast_time);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* Receive response */
  manager.on_broadcast_response(0xB8, 0x03, true);

  /* Timeout the window - should complete */
  esphome_hal_double_set_millis(broadcast_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
  CHECK_TRUE(callback_called);
}

TEST(autodiscovery_manager, gea2_discovery_completes_on_timeout)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t gea3_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(gea3_time);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* GEA3 times out with no response → GEA2 */
  esphome_hal_double_set_millis(gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();  /* → GEA2_BROADCAST_PENDING */
  expect_gea2_broadcast_read();
  uint32_t gea2_time = gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100;
  esphome_hal_double_set_millis(gea2_time);
  manager.run();  /* → GEA2_BROADCAST_WAITING */

  /* Receive GEA2 response */
  manager.on_broadcast_response(0xB8, 0x03, false);

  /* Timeout the window - should complete */
  esphome_hal_double_set_millis(gea2_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_gea2_protocol());
}

TEST(autodiscovery_manager, run_does_nothing_when_complete)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();
  expect_gea3_broadcast_read();
  uint32_t broadcast_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(broadcast_time);
  manager.run();

  manager.on_broadcast_response(0xB8, 0x03, true);
  esphome_hal_double_set_millis(broadcast_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());

  /* Further runs should be no-ops */
  esphome_hal_double_set_millis(esphome_hal_double_get_millis() + 10000);
  manager.run();
  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Retry logic                                                          */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, run_retries_gea3_then_gea2_on_timeout)
{
  init_both_uart();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t gea3_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(gea3_time);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* Timeout with no response → GEA2 */
  esphome_hal_double_set_millis(gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();  /* → GEA2_BROADCAST_PENDING */
  expect_gea2_broadcast_read();
  uint32_t gea2_time = gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100;
  esphome_hal_double_set_millis(gea2_time);
  manager.run();  /* → GEA2_BROADCAST_WAITING */

  CHECK_EQUAL(1, manager.get_retry_count());
}

TEST(autodiscovery_manager, gea3_only_retries_gea3)
{
  init_gea3_only();
  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING */
  expect_gea3_broadcast_read();
  uint32_t gea3_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(gea3_time);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  /* Timeout with no response → retry GEA3 */
  esphome_hal_double_set_millis(gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();  /* → GEA3_BROADCAST_PENDING (retry) */
  expect_gea3_broadcast_read();
  uint32_t gea3_time2 = gea3_time + AUTODISCOVERY_BROADCAST_WINDOW_MS + 100;
  esphome_hal_double_set_millis(gea3_time2);
  manager.run();  /* → GEA3_BROADCAST_WAITING */

  CHECK_EQUAL(1, manager.get_retry_count());
}

/* ------------------------------------------------------------------ */
/* Null callback handling                                               */
/* ------------------------------------------------------------------ */

TEST(autodiscovery_manager, completion_with_null_callback_does_not_crash)
{
  manager.init(&gea3_client.interface,
               &gea2_client.interface,
               &gea2_adapter.interface,
               true, true,
               nullptr);  /* No callback */

  esphome_hal_double_set_millis(AUTODISCOVERY_STARTUP_DELAY_MS);
  manager.run();
  expect_gea3_broadcast_read();
  uint32_t broadcast_time = AUTODISCOVERY_STARTUP_DELAY_MS + 100;
  esphome_hal_double_set_millis(broadcast_time);
  manager.run();

  manager.on_broadcast_response(0xB8, 0x03, true);
  esphome_hal_double_set_millis(broadcast_time + AUTODISCOVERY_BROADCAST_WINDOW_MS);
  manager.run();

  CHECK_EQUAL(AUTODISCOVERY_COMPLETE, manager.get_state());
}
