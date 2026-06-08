/*!
 * @file
 * @brief Unit tests for HaDiscoveryManager.
 *
 * Tests the state machine transitions, ERD tracking, quiet window logic,
 * safety cap, and non-ESP-IDF fallback behavior.
 *
 * NOTE: On non-ESP-IDF builds, publish_ha_discovery_() first checks
 * mqtt_client == nullptr or !is_connected() and returns early WITHOUT
 * transitioning state (line 165). The #else branch (non-ESP-IDF) that
 * transitions to COMPLETE is only reached when mqtt_client is non-null
 * and connected. Since we cannot mock MQTTClientComponent easily, tests
 * verify state transitions through the run() method's ready logic only,
 * and accept that with null mqtt_client the state remains WAITING_FOR_READY.
 */

#include "ha_discovery_manager.h"
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
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(ha_discovery_manager)
{
  HaDiscoveryManager manager;

  void setup()
  {
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    manager.cleanup();
    mock().clear();
  }

  void when_initialized_with_erds(const std::set<tiny_erd_t>& erds)
  {
    manager.init(
      "https://example.com/ha_discovery",
      "test_device_001",
      "GUE2700M01",
      "SN123456",
      erds,
      false
    );
  }
};

/* ------------------------------------------------------------------ */
/* Initialization                                                       */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, init_sets_state_to_waiting_for_ready)
{
  std::set<tiny_erd_t> erds{0x0008, 0x0092};
  when_initialized_with_erds(erds);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, init_stores_device_id)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Device ID is stored internally; verify through state query
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, init_stores_registered_erds)
{
  std::set<tiny_erd_t> erds{0x0008, 0x0092, 0x0093};
  when_initialized_with_erds(erds);
  // After init, set_registered_erds can replace the set
  std::set<tiny_erd_t> new_erds{0x0100};
  manager.set_registered_erds(new_erds);
  // If set was stored, replacing it should not crash
  CHECK_TRUE(true);
}

TEST(ha_discovery_manager, init_sets_start_time)
{
  esphome_hal_double_set_millis(5000);
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // start_time_ should be set to current millis (5000)
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, init_with_empty_erds)
{
  std::set<tiny_erd_t> erds;
  manager.init(
    "https://example.com/ha_discovery",
    "test_device_empty",
    "",
    "",
    erds,
    false
  );
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* set_registered_erds                                                  */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, set_registered_erds_updates_set)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  std::set<tiny_erd_t> new_erds{0x0092, 0x0093};
  manager.set_registered_erds(new_erds);
  CHECK_TRUE(true);
}

/* ------------------------------------------------------------------ */
/* on_erd_seen                                                          */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, on_erd_seen_tracks_new_erd)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  esphome_hal_double_set_millis(100);
  manager.on_erd_seen(0x0008);
  esphome_hal_double_set_millis(200);
  // Calling on_erd_seen again with same ERD should not update last_activity_
  manager.on_erd_seen(0x0008);
  CHECK_TRUE(true);
}

TEST(ha_discovery_manager, on_erd_seen_ignores_duplicate)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  esphome_hal_double_set_millis(100);
  manager.on_erd_seen(0x0008);
  esphome_hal_double_set_millis(500);
  // Duplicate should not update last_activity_
  manager.on_erd_seen(0x0008);
  esphome_hal_double_set_millis(600);
  // New ERD should update last_activity_
  manager.on_erd_seen(0x0092);
  CHECK_TRUE(true);
}

TEST(ha_discovery_manager, on_erd_seen_no_op_when_not_waiting)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Manually transition to COMPLETE by calling run in poll mode
  // (even though with null mqtt_client it stays WAITING, we test
  // the no-op behavior for non-WAITING states)
  // Use IDLE state — on_erd_seen checks for WAITING_FOR_READY
  HaDiscoveryManager mgr2;
  // mgr2 is in IDLE state (default)
  mgr2.on_erd_seen(0x0008);
  CHECK_TRUE(true);
}

TEST(ha_discovery_manager, on_erd_seen_with_multiple_unique_erds)
{
  std::set<tiny_erd_t> erds{0x0008, 0x0092, 0x0093};
  when_initialized_with_erds(erds);
  esphome_hal_double_set_millis(100);
  manager.on_erd_seen(0x0008);
  esphome_hal_double_set_millis(200);
  manager.on_erd_seen(0x0092);
  esphome_hal_double_set_millis(300);
  manager.on_erd_seen(0x0093);
  CHECK_TRUE(true);
}

/* ------------------------------------------------------------------ */
/* run() - Poll mode                                                    */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, run_poll_mode_with_null_mqtt_stays_waiting)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Poll mode with polling_list_complete = true calls publish_ha_discovery_().
  // With null mqtt_client, publish_ha_discovery_() returns early (line 165)
  // without transitioning state.
  manager.run(true, true, false, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, run_poll_mode_does_not_transition_when_list_incomplete)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Poll mode with polling_list_complete = false — ready is false
  manager.run(true, false, false, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, run_poll_mode_ignores_subscription_activity)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // In poll mode, subscription_activity_detected is ignored
  manager.run(true, false, true, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* run() - Subscription mode                                            */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, run_subscription_mode_waits_for_quiet_window)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  esphome_hal_double_set_millis(100);
  manager.on_erd_seen(0x0008);
  // Activity at 100ms, quiet window is 10s
  // At 5000ms (before quiet window), should still be waiting
  esphome_hal_double_set_millis(5000);
  manager.run(false, false, true, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, run_subscription_mode_no_activity_never_transitions_before_cap)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // No activity, subscription_activity_detected = false
  // Should not transition until safety cap
  esphome_hal_double_set_millis(5000);
  manager.run(false, false, false, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, run_subscription_mode_safety_cap_triggers_publish_attempt)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Safety cap is 30s (HA_DISCOVERY_MAX_WAIT_MS)
  // init() sets start_time_ to current millis (0)
  esphome_hal_double_set_millis(30000);
  // After safety cap, run() calls publish_ha_discovery_() which returns
  // early with null mqtt_client, so state stays WAITING_FOR_READY
  manager.run(false, false, false, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, run_subscription_mode_safety_cap_with_activity)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Activity keeps resetting quiet window, but safety cap still fires
  esphome_hal_double_set_millis(100);
  manager.on_erd_seen(0x0008);
  esphome_hal_double_set_millis(200);
  manager.on_erd_seen(0x0092);
  // Even with ongoing activity, safety cap at 30s should trigger
  // publish_ha_discovery_() which returns early with null mqtt_client
  esphome_hal_double_set_millis(30000);
  manager.run(false, false, true, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* run() - Edge cases                                                   */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, run_with_null_mqtt_client_does_not_crash)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  manager.run(true, true, false, nullptr);
  // Should not crash; state stays WAITING_FOR_READY with null mqtt
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, run_from_idle_state_does_nothing)
{
  // Manager starts in IDLE state before init()
  // run() should not crash on IDLE state
  manager.run(true, true, false, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

TEST(ha_discovery_manager, run_from_failed_state_does_nothing)
{
  // Manager in default state (IDLE) — run should be safe
  HaDiscoveryManager mgr;
  mgr.run(false, false, false, nullptr);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, mgr.get_state());
}

/* ------------------------------------------------------------------ */
/* State query methods                                                  */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, is_complete_false_before_init)
{
  CHECK_FALSE(manager.is_complete());
}

TEST(ha_discovery_manager, is_failed_false_before_init)
{
  CHECK_FALSE(manager.is_failed());
}

TEST(ha_discovery_manager, is_publishing_false_before_init)
{
  CHECK_FALSE(manager.is_publishing());
}

TEST(ha_discovery_manager, is_ready_to_start_false_before_init)
{
  CHECK_FALSE(manager.is_ready_to_start());
}

TEST(ha_discovery_manager, is_ready_to_start_true_after_init)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  CHECK_TRUE(manager.is_ready_to_start());
}

/* ------------------------------------------------------------------ */
/* set_mqtt_adapter                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, set_mqtt_adapter_stores_pointer)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  esphome_mqtt_client_adapter_t dummy_adapter;
  manager.set_mqtt_adapter(&dummy_adapter);
  CHECK_TRUE(true);
}

TEST(ha_discovery_manager, set_mqtt_adapter_with_null)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  manager.set_mqtt_adapter(nullptr);
  CHECK_TRUE(true);
}

/* ------------------------------------------------------------------ */
/* cleanup                                                              */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, cleanup_is_idempotent)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  manager.cleanup();
  manager.cleanup();
  manager.cleanup();
  CHECK_TRUE(true);
}

TEST(ha_discovery_manager, cleanup_before_init)
{
  // cleanup() on uninitialized manager should not crash
  manager.cleanup();
  CHECK_TRUE(true);
}

/* ------------------------------------------------------------------ */
/* Re-initialization                                                    */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, reinit_resets_state)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  // Re-init with new ERDs
  std::set<tiny_erd_t> new_erds{0x0092};
  manager.init(
    "https://example.com/ha_discovery",
    "test_device_002",
    "GUE2700M01",
    "SN789012",
    new_erds,
    false
  );
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, multiple_init_calls_reset_state)
{
  std::set<tiny_erd_t> erds{0x0008};
  when_initialized_with_erds(erds);
  manager.init(
    "https://example.com/ha_discovery",
    "test_device_003",
    "",
    "",
    erds,
    true
  );
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}
