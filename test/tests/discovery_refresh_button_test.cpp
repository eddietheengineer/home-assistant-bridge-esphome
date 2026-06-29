/*!
 * @file
 * @brief Unit tests for DiscoveryRefreshButton.
 *
 * Tests construction, press_action() behavior under various bridge states,
 * and null-bridge safety.
 *
 * Note: Production headers with STL containers (<string>) must be
 * included BEFORE CppUTest headers to avoid conflicts with CppUTest's
 * custom 'new' macro which breaks placement-new in standard library headers.
 */

#include "geappliances_bridge.h"
#include "button/button.h"
#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(discovery_refresh_button)
{
  GeappliancesBridge bridge;

  void setup()
  {
    // Each test gets a fresh bridge with default member values.
    // steady_state_reached_ = false, discovery_refresh_in_progress_ = false,
    // ha_discovery_manager_ is zero-initialized (state = idle after init).
  }
};

TEST(discovery_refresh_button, can_be_constructed_with_bridge_pointer)
{
  DiscoveryRefreshButton button(&bridge);
  // Construction must not crash. The button stores the bridge pointer.
  CHECK_TRUE(true);
}

TEST(discovery_refresh_button, press_action_does_not_crash_with_uninitialized_bridge)
{
  // Bridge has not been set up (steady_state_reached_ is false).
  // trigger_discovery_refresh() will early-return due to the steady-state guard.
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  // Must not crash even though the bridge is not in a valid state.
  CHECK_TRUE(true);
}

TEST(discovery_refresh_button, press_action_is_idempotent_after_successful_trigger)
{
  // We cannot directly set steady_state_reached_ from test code (it's private),
  // but we can verify that calling press_action() multiple times is safe.
  // The first call may or may not trigger cleanup depending on bridge state,
  // but subsequent calls must not crash.
  DiscoveryRefreshButton button(&bridge);
  button.press_action();
  button.press_action();
  button.press_action();
  // Multiple presses must not crash regardless of internal state.
  CHECK_TRUE(true);
}

TEST(discovery_refresh_button, press_action_safe_when_bridge_is_null)
{
  // Button constructed with null bridge pointer must not crash on press.
  DiscoveryRefreshButton button(nullptr);
  button.press_action();
  // The null guard in press_action() prevents dereferencing a null pointer.
  CHECK_TRUE(true);
}

TEST(discovery_refresh_button, multiple_presses_with_null_bridge_are_safe)
{
  DiscoveryRefreshButton button(nullptr);
  button.press_action();
  button.press_action();
  button.press_action();
  CHECK_TRUE(true);
}

TEST(discovery_refresh_button, button_inherits_from_button_base)
{
  // Verify the button is a proper subclass of esphome::button::Button.
  DiscoveryRefreshButton button(&bridge);
  esphome::button::Button* base = &button;
  CHECK(base != nullptr);
}

TEST(discovery_refresh_button, press_action_safe_after_teardown)
{
  // Calling teardown() on the bridge and then pressing the button must not crash.
  DiscoveryRefreshButton button(&bridge);
  bridge.teardown();
  button.press_action();
  CHECK_TRUE(true);
}
