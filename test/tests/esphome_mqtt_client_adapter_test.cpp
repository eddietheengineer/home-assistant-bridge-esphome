/*!
 * @file
 * @brief Unit tests for the ESPHome MQTT client adapter (debug-log mode).
 *
 * The adapter no longer publishes to MQTT.  All publish paths are replaced
 * with ESP_LOGD debug messages.  These tests verify that the adapter
 * functions without crashing, correctly filters ERDs through the registry,
 * and that all MQTT-specific entry points are safe no-ops.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"

#include <string>
#include <set>

/* ------------------------------------------------------------------ */
/* Test group                                                          */
/* ------------------------------------------------------------------ */

TEST_GROUP(esphome_mqtt_client_adapter)
{
  esphome_mqtt_client_adapter_t adapter;
  std::string device_id_str;
  esphome::geappliances_bridge::ErdRegistry erd_registry;

  void setup()
  {
    mock().strictOrder();
    device_id_str = "test_device";
    erd_registry = esphome::geappliances_bridge::ErdRegistry();
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    esphome_mqtt_client_adapter_destroy(&adapter);
    mock().clear();
  }

  void init_adapter()
  {
    esphome_mqtt_client_adapter_init(&adapter, device_id_str.c_str());
  }

  void init_adapter_with_registry()
  {
    init_adapter();
    esphome_mqtt_client_adapter_set_erd_registry(&adapter, &erd_registry);
  }
};

/* ------------------------------------------------------------------ */
/* Initialization                                                       */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, init_sets_api_pointer)
{
  init_adapter();
  CHECK(adapter.interface.api != nullptr);
}

TEST(esphome_mqtt_client_adapter, init_creates_device_id)
{
  init_adapter();
  // Calling register_erd should not crash even without registry
  adapter.interface.api->register_erd(&adapter.interface, 0x0008);
}

/* ------------------------------------------------------------------ */
/* register_erd                                                         */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, register_erd_tracks_in_output_set)
{
  tiny_erd_t valid[] = {0x0008};
  erd_registry.set_valid_erds(valid, 1);
  init_adapter_with_registry();
  adapter.interface.api->register_erd(&adapter.interface, 0x0008);
  CHECK_EQUAL(1u, erd_registry.registered_erd_count());
  CHECK_EQUAL(0x0008u, erd_registry.registered_erd(0));
}

TEST(esphome_mqtt_client_adapter, register_erd_without_output_set)
{
  init_adapter();
  adapter.interface.api->register_erd(&adapter.interface, 0x0008);
}

/* ------------------------------------------------------------------ */
/* update_erd_write_result                                              */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, update_erd_write_result_success_no_crash)
{
  init_adapter();

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, true, 0);
  // global_mqtt_client is null in tests — publish is skipped, no crash.
}

TEST(esphome_mqtt_client_adapter, update_erd_write_result_failure_no_crash)
{
  init_adapter();

  adapter.interface.api->update_erd_write_result(&adapter.interface, 0x0092, false, 3);
  // global_mqtt_client is null in tests — publish is skipped, no crash.
}
/* ------------------------------------------------------------------ */
/* destroy                                                              */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, destroy_frees_resources)
{
  init_adapter();
  /* destroy happens in teardown */
}

/* ------------------------------------------------------------------ */
/* notify_connected / notify_disconnected — no-ops                      */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, notify_connected_is_noop)
{
  init_adapter();
  esphome_mqtt_client_adapter_notify_connected(&adapter);
  // Should not crash
}

TEST(esphome_mqtt_client_adapter, notify_disconnected_is_noop)
{
  init_adapter();
  esphome_mqtt_client_adapter_notify_disconnected(&adapter);
  // Should not crash
}

TEST(esphome_mqtt_client_adapter, subscribe_write_topic_is_noop)
{
  init_adapter();
  esphome_mqtt_client_adapter_subscribe_write_topic(&adapter);
  // Should not crash
}

TEST(esphome_mqtt_client_adapter, drain_pending_updates_returns_zero)
{
  init_adapter();

  size_t remaining = esphome_mqtt_client_adapter_drain_pending_updates(&adapter);
  CHECK_EQUAL(0u, remaining);
}

TEST(esphome_mqtt_client_adapter, get_pending_update_count_returns_zero)
{
  init_adapter();

  CHECK_EQUAL(0u, esphome_mqtt_client_adapter_get_pending_update_count(&adapter));
}

/* ------------------------------------------------------------------ */
/* publish helper                                                       */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, publish_helper_does_not_crash)
{
  init_adapter();
  esphome_mqtt_client_adapter_publish(&adapter, "test/topic", "test_payload", true);
  // Should not crash — logs via ESP_LOGD
}

/* ------------------------------------------------------------------ */
/* on_write_request / on_mqtt_disconnect event accessors                */
/* ------------------------------------------------------------------ */

TEST(esphome_mqtt_client_adapter, on_write_request_returns_event)
{
  init_adapter();
  i_tiny_event_t* evt = adapter.interface.api->on_write_request(&adapter.interface);
  CHECK(evt != nullptr);
}

TEST(esphome_mqtt_client_adapter, on_mqtt_disconnect_returns_event)
{
  init_adapter();
  i_tiny_event_t* evt = adapter.interface.api->on_mqtt_disconnect(&adapter.interface);
  CHECK(evt != nullptr);
}
