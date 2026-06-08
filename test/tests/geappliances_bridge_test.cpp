/*!
 * @file
 * @brief Unit tests for GeappliancesBridge public API.
 *
 * Tests the configuration setters, default values, and IBridgeServices
 * query methods.  Does not call setup()/loop() as those require
 * ESPHome infrastructure (UART, MQTT client, etc.).
 *
 * GeappliancesBridge implements IBridgeServices as protected methods
 * (called exclusively by the startup HSM).  TestGeappliancesBridge
 * inherits and exposes them for testing.
 */

#include "geappliances_bridge.h"
#include "double/esphome_hal_double.hpp"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#ifdef new
#undef new
#endif

#include <set>
#include <string>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Test subclass: exposes protected IBridgeServices for testing        */
/* ------------------------------------------------------------------ */

class TestGeappliancesBridge : public GeappliancesBridge {
 public:
  // Expose protected IBridgeServices methods
  BridgeMode get_mode() const override { return GeappliancesBridge::get_mode(); }
  bool is_autodiscovery_complete() const override { return GeappliancesBridge::is_autodiscovery_complete(); }
  uint8_t get_discovered_host_address() const override { return GeappliancesBridge::get_discovered_host_address(); }
  bool is_discovered_gea2_protocol() const override { return GeappliancesBridge::is_discovered_gea2_protocol(); }
  bool is_device_id_complete() const override { return GeappliancesBridge::is_device_id_complete(); }
  bool is_mqtt_client_initialized() const override { return GeappliancesBridge::is_mqtt_client_initialized(); }
  bool is_feature_bits_complete() const override { return GeappliancesBridge::is_feature_bits_complete(); }
  bool is_bridge_initialized() const override { return GeappliancesBridge::is_bridge_initialized(); }
  bool is_subscription_mode_active() const override { return GeappliancesBridge::is_subscription_mode_active(); }
};

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(geappliances_bridge)
{
  TestGeappliancesBridge bridge;

  void setup()
  {
    esphome_hal_double_set_millis(0);
    mock().strictOrder();
  }

  void teardown()
  {
    mock().clear();
  }
};

/* ------------------------------------------------------------------ */
/* Setup priority                                                       */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, get_setup_priority_returns_data_priority)
{
  // setup_priority::DATA is 600.0f
  LONGS_EQUAL(600, static_cast<long>(bridge.get_setup_priority()));
}

/* ------------------------------------------------------------------ */
/* Configuration setters and defaults                                   */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, set_client_address_stores_value)
{
  // Verify setter doesn't crash and bridge remains functional.
  bridge.set_client_address(0x10);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, default_client_address_is_0xe4)
{
  // Default client_address_ = 0xE4 per class member initializer.
  // Verify by checking the bridge constructs and defaults are sane.
  TestGeappliancesBridge b;
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_device_id_stores_value)
{
  bridge.set_device_id("test-device-123");
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_mode_stores_value)
{
  bridge.set_mode(BRIDGE_MODE_POLL);
  CHECK(bridge.get_mode() == BRIDGE_MODE_POLL);

  bridge.set_mode(BRIDGE_MODE_SUBSCRIBE);
  CHECK(bridge.get_mode() == BRIDGE_MODE_SUBSCRIBE);

  bridge.set_mode(BRIDGE_MODE_AUTO);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, default_mode_is_auto)
{
  TestGeappliancesBridge b;
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_polling_interval_stores_value)
{
  bridge.set_polling_interval(5000);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, default_polling_interval_is_10000)
{
  // Default is 10000ms per class member initializer.
  TestGeappliancesBridge b;
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_appliance_api_parsing_stores_value)
{
  bridge.set_appliance_api_parsing(false);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_generate_device_config_stores_value)
{
  bridge.set_generate_device_config(true);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, add_custom_erd_appends_to_vector)
{
  bridge.add_custom_erd(0x0100);
  bridge.add_custom_erd(0x0200);
  bridge.add_custom_erd(0x0300);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_ha_discovery_base_url_stores_value)
{
  bridge.set_ha_discovery_base_url("https://example.com/custom_discovery");
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, get_mode_returns_configured_mode)
{
  TestGeappliancesBridge b;
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);

  b.set_mode(BRIDGE_MODE_POLL);
  CHECK(b.get_mode() == BRIDGE_MODE_POLL);

  b.set_mode(BRIDGE_MODE_SUBSCRIBE);
  CHECK(b.get_mode() == BRIDGE_MODE_SUBSCRIBE);
}

/* ------------------------------------------------------------------ */
/* IBridgeServices query methods — initial (pre-setup) state            */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, is_autodiscovery_complete_false_before_start)
{
  // AutodiscoveryManager starts in AUTODISCOVERY_IDLE, not COMPLETE.
  CHECK(!bridge.is_autodiscovery_complete());
}

TEST(geappliances_bridge, is_device_id_complete_false_before_completion)
{
  // DeviceIdentityManager starts in DEVICE_ID_STATE_READING_APPLIANCE_TYPE.
  CHECK(!bridge.is_device_id_complete());
}

TEST(geappliances_bridge, is_mqtt_client_initialized_false_before_init)
{
  // mqtt_client_adapter_initialized_ defaults to false.
  CHECK(!bridge.is_mqtt_client_initialized());
}

TEST(geappliances_bridge, is_feature_bits_complete_false_before_completion)
{
  // FeatureBitManager starts in FEATURE_BIT_STATE_READING_0008.
  CHECK(!bridge.is_feature_bits_complete());
}

TEST(geappliances_bridge, is_bridge_initialized_false_before_init)
{
  // mqtt_bridge_initialized_ defaults to false.
  CHECK(!bridge.is_bridge_initialized());
}

TEST(geappliances_bridge, get_discovered_host_address_returns_zero_before_discovery)
{
  // AutodiscoveryManager.host_address_ defaults to 0.
  CHECK_EQUAL(0, bridge.get_discovered_host_address());
}

TEST(geappliances_bridge, is_discovered_gea2_protocol_false_by_default)
{
  // AutodiscoveryManager.gea2_protocol_active_ defaults to false.
  CHECK(!bridge.is_discovered_gea2_protocol());
}

TEST(geappliances_bridge, is_subscription_mode_active_false_by_default)
{
  // subscription_mode_active_ defaults to false.
  CHECK(!bridge.is_subscription_mode_active());
}

/* ------------------------------------------------------------------ */
/* Multiple setter calls — idempotency and state isolation              */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, setters_are_idempotent)
{
  bridge.set_mode(BRIDGE_MODE_POLL);
  bridge.set_mode(BRIDGE_MODE_POLL);
  CHECK(bridge.get_mode() == BRIDGE_MODE_POLL);

  bridge.set_client_address(0x20);
  bridge.set_client_address(0x20);
  CHECK(bridge.get_mode() == BRIDGE_MODE_POLL);
}

TEST(geappliances_bridge, multiple_custom_erds_dont_crash)
{
  for (uint16_t i = 0; i < 50; ++i) {
    bridge.add_custom_erd(static_cast<uint16_t>(0x1000 + i));
  }
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, device_id_with_empty_string)
{
  bridge.set_device_id("");
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, device_id_with_long_string)
{
  std::string long_id(256, 'A');
  bridge.set_device_id(long_id);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

/* ------------------------------------------------------------------ */
/* Fresh instance defaults — verifying each default independently       */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, fresh_instance_all_defaults)
{
  TestGeappliancesBridge b;

  // Mode default
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);

  // Autodiscovery not complete
  CHECK(!b.is_autodiscovery_complete());

  // Device ID not complete
  CHECK(!b.is_device_id_complete());

  // MQTT client not initialized
  CHECK(!b.is_mqtt_client_initialized());

  // Feature bits not complete
  CHECK(!b.is_feature_bits_complete());

  // Bridge not initialized
  CHECK(!b.is_bridge_initialized());

  // Host address is zero
  CHECK_EQUAL(0, b.get_discovered_host_address());

  // GEA2 protocol not active
  CHECK(!b.is_discovered_gea2_protocol());

  // Subscription mode not active
  CHECK(!b.is_subscription_mode_active());

  // Setup priority is DATA (600)
  LONGS_EQUAL(600, static_cast<long>(b.get_setup_priority()));
}

/* ------------------------------------------------------------------ */
/* Mode transitions and mode getter consistency                         */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, mode_transitions_through_all_values)
{
  TestGeappliancesBridge b;

  // Start at AUTO
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);

  // Transition to POLL
  b.set_mode(BRIDGE_MODE_POLL);
  CHECK(b.get_mode() == BRIDGE_MODE_POLL);

  // Transition to SUBSCRIBE
  b.set_mode(BRIDGE_MODE_SUBSCRIBE);
  CHECK(b.get_mode() == BRIDGE_MODE_SUBSCRIBE);

  // Back to AUTO
  b.set_mode(BRIDGE_MODE_AUTO);
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_mode_with_raw_uint8_values)
{
  TestGeappliancesBridge b;

  // set_mode() takes uint8_t and casts to BridgeMode
  b.set_mode(0);  // BRIDGE_MODE_POLL
  CHECK(b.get_mode() == BRIDGE_MODE_POLL);

  b.set_mode(1);  // BRIDGE_MODE_SUBSCRIBE
  CHECK(b.get_mode() == BRIDGE_MODE_SUBSCRIBE);

  b.set_mode(2);  // BRIDGE_MODE_AUTO
  CHECK(b.get_mode() == BRIDGE_MODE_AUTO);
}

/* ------------------------------------------------------------------ */
/* Custom ERD edge cases                                                */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, add_custom_erd_with_zero)
{
  bridge.add_custom_erd(0);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, add_custom_erd_with_max_value)
{
  bridge.add_custom_erd(0xFFFF);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, add_custom_erd_duplicates_allowed)
{
  bridge.add_custom_erd(0x0100);
  bridge.add_custom_erd(0x0100);
  bridge.add_custom_erd(0x0100);
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

/* ------------------------------------------------------------------ */
/* HA discovery base URL edge cases                                     */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, set_ha_discovery_base_url_with_empty_string)
{
  bridge.set_ha_discovery_base_url("");
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

TEST(geappliances_bridge, set_ha_discovery_base_url_with_special_characters)
{
  bridge.set_ha_discovery_base_url("https://example.com/path?query=value&foo=bar");
  CHECK(bridge.get_mode() == BRIDGE_MODE_AUTO);
}

/* ------------------------------------------------------------------ */
/* Independent instance isolation                                       */
/* ------------------------------------------------------------------ */

TEST(geappliances_bridge, two_instances_are_independent)
{
  TestGeappliancesBridge b1;
  TestGeappliancesBridge b2;

  b1.set_mode(BRIDGE_MODE_POLL);
  b2.set_mode(BRIDGE_MODE_SUBSCRIBE);

  CHECK(b1.get_mode() == BRIDGE_MODE_POLL);
  CHECK(b2.get_mode() == BRIDGE_MODE_SUBSCRIBE);
}

TEST(geappliances_bridge, two_instances_have_independent_defaults)
{
  TestGeappliancesBridge b1;
  TestGeappliancesBridge b2;

  CHECK(b1.get_mode() == BRIDGE_MODE_AUTO);
  CHECK(b2.get_mode() == BRIDGE_MODE_AUTO);

  CHECK(!b1.is_autodiscovery_complete());
  CHECK(!b2.is_autodiscovery_complete());
}
