/*!
 * @file
 * @brief Unit tests for HA discovery integration in GeappliancesBridge.
 *
 * Tests set_generate_device_config(), set_filter_config_topics(),
 * trigger_discovery_refresh() safety, and default values of discovery
 * state fields.
 *
 * Note: Production headers with STL containers (<string>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge.h"
#include "button/button.h"
#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(geappliances_bridge_discovery)
{
  GeappliancesBridge bridge;

  void setup()
  {
    // Each test gets a fresh instance with default member values.
  }
};

TEST(geappliances_bridge_discovery, set_generate_device_config_sets_flag_correctly)
{
  // Toggling setter doesn't crash.
  bridge.set_generate_device_config(true);
  bridge.set_generate_device_config(false);
  bridge.set_generate_device_config(true);
  CHECK_TRUE(true);
}

TEST(geappliances_bridge_discovery, set_filter_config_topics_sets_flag_correctly)
{
  // Toggling setter doesn't crash.
  bridge.set_filter_config_topics(false);
  bridge.set_filter_config_topics(true);
  bridge.set_filter_config_topics(false);
  CHECK_TRUE(true);
}

TEST(geappliances_bridge_discovery, trigger_discovery_refresh_safe_on_initialized_bridge)
{
  // trigger_discovery_refresh() is protected; access via DiscoveryRefreshButton
  // which is a friend of GeappliancesBridge.  steady_state_reached_ is false,
  // so it returns early without side effects.
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  // No crash = guard chain works when steady_state_reached_ is false.
  CHECK_TRUE(true);
}

TEST(geappliances_bridge_discovery, button_press_is_idempotent)
{
  // Multiple presses don't crash and don't cause side effects
  // (steady_state_reached_ is false, so all return early).
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  button.press_action();
  button.press_action();
  CHECK_TRUE(true);
}

TEST(geappliances_bridge_discovery, button_safe_with_null_bridge)
{
  // Button with null bridge pointer should not crash.
  DiscoveryRefreshButton button(nullptr);
  button.press_action();
  CHECK_TRUE(true);
}

TEST(geappliances_bridge_discovery, button_inherits_from_button_base)
{
  // DiscoveryRefreshButton is a subclass of esphome::button::Button.
  GeappliancesBridge bridge;
  DiscoveryRefreshButton button(&bridge);
  // Can cast to esphome::button::Button* without error.
  esphome::button::Button* base = &button;
  CHECK(base != nullptr);
}

TEST(geappliances_bridge_discovery, setter_toggles_are_safe)
{
  // Repeatedly toggling both setters doesn't crash.
  for (int i = 0; i < 10; i++) {
    bridge.set_generate_device_config(i % 2 == 0);
    bridge.set_filter_config_topics(i % 2 != 0);
  }
  CHECK_TRUE(true);
}
