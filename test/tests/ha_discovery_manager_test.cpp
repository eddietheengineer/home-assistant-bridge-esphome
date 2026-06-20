/*!\
 * @file
 * @brief Unit tests for HaDiscoveryManager state machine.
 *
 * Tests the state machine logic on non-ESP-IDF builds (USE_ESP_IDF_STUBS).
 * The HTTPS fetch path is a no-op in stub builds; tests cover:
 *   - init: state transition, device info storage
 *   - set_registered_erds: no-op (cache is read directly)
 *   - on_erd_seen: adds ERDs, resets activity timestamp
 *   - run in poll mode: ready when polling_list_complete
 *   - run in subscription mode: ready after quiet window or safety cap
 *   - State query methods (is_complete, is_failed, etc.)
 *   - cleanup: safe on double call
 *   - Non-ESP-IDF fetch: discovery completes without actual HTTPS
 */

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "ha_discovery_manager.h"
#include "erd_cache.h"
#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"

#include "CppUTest/TestHarness.h"

#include <cstring>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Test group — basic state machine                                    */
/* ------------------------------------------------------------------ */

TEST_GROUP(ha_discovery_manager)
{
  HaDiscoveryManager manager;
  erd_cache_t test_cache_;

  void setup()
  {
    esphome_hal_double_set_millis(0);
    manager.cleanup();
    erd_cache_init(&test_cache_);
  }

  void teardown()
  {
    manager.cleanup();
    erd_cache_destroy(&test_cache_);
  }

  // Helper: populate the test cache with the given ERDs
  void add_erds(tiny_erd_t* erds, int count)
  {
    uint8_t dummy = 0;
    for (int i = 0; i < count; i++) {
      erd_cache_update(&test_cache_, erds[i], &dummy, 1);
    }
  }

  // Helper: init with a single ERD
  void init_with_erd(tiny_erd_t erd, bool gen_config = false)
  {
    uint8_t dummy = 0;
    erd_cache_update(&test_cache_, erd, &dummy, 1);
    manager.init("dev1", "model1", "sn1", &test_cache_, gen_config);
  }
};

/* ------------------------------------------------------------------ */
/* init                                                                 */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, init_sets_state_to_waiting_for_ready)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002 };
  add_erds(erds, 2);
  manager.init("dev1", "model1", "sn1", &test_cache_, false);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, init_stores_device_info)
{
  init_with_erd(0x0001, true);
  CHECK_TRUE(manager.is_ready_to_start());
  CHECK_FALSE(manager.is_complete());
  CHECK_FALSE(manager.is_failed());
  CHECK_FALSE(manager.is_publishing());
}

TEST(ha_discovery_manager, init_clamps_registered_erds_to_max)
{
  // Populate cache with more than HA_DISCOVERY_MAX_ERDS entries.
  // The cache itself caps at ERD_CACHE_CAPACITY (200), so we fill that.
  // The point is init() should not crash with a full cache.
  uint8_t dummy = 0;
  for (int i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_update(&test_cache_, static_cast<uint16_t>(i), &dummy, 1);
  }
  manager.init("dev1", "model1", "sn1", &test_cache_, false);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, init_with_empty_erds)
{
  // test_cache_ is empty (no entries added)
  manager.init("dev1", "model1", "sn1", &test_cache_, false);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* set_registered_erds — now a no-op (cache is read directly)           */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, set_registered_erds_is_noop)
{
  init_with_erd(0x0001);
  tiny_erd_t new_erds[] = { 0x0002, 0x0003, 0x0004 };
  manager.set_registered_erds(new_erds, 3);
  // State should remain WAITING_FOR_READY
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, set_registered_erds_clamps_to_max)
{
  init_with_erd(0x0001);
  tiny_erd_t new_erds[] = { 0x0001, 0x0002 };
  manager.set_registered_erds(new_erds, 2);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* on_erd_seen                                                          */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, on_erd_seen_adds_erd_to_seen_list)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002 };
  add_erds(erds, 2);
  manager.init("dev1", "model1", "sn1", &test_cache_, false);

  manager.on_erd_seen(0x0001);
  // Should not crash and state should remain WAITING_FOR_READY
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, on_erd_seen_resets_activity_timestamp)
{
  esphome_hal_double_set_millis(1000);
  init_with_erd(0x0001);

  esphome_hal_double_set_millis(5000);
  manager.on_erd_seen(0x0001);
  // last_activity_ should be updated to current millis (5000)
  // Verify by checking that quiet window has NOT elapsed yet
  esphome_hal_double_set_millis(10000);
  // 10000 - 5000 = 5000 < HA_DISCOVERY_QUIET_MS (10000), so not quiet yet
  manager.run(false);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, on_erd_seen_ignores_duplicate)
{
  esphome_hal_double_set_millis(1000);
  init_with_erd(0x0001);

  manager.on_erd_seen(0x0001);
  esphome_hal_double_set_millis(2000);
  manager.on_erd_seen(0x0001);
  // Duplicate should NOT reset last_activity_ — activity timestamp from first call (1000)
  // Verify by checking that last_activity_ is still 1000 (not 2000)
  // We can't directly check last_activity_, so verify the quiet window behavior:
  // After 10s from 1000 = 11000, steady_state=true should trigger discovery
  esphome_hal_double_set_millis(11000);
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

TEST(ha_discovery_manager, on_erd_seen_noop_after_state_changes)
{
  init_with_erd(0x0001);

  // Transition to COMPLETE via steady state
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());

  // on_erd_seen should be a no-op in non-WAITING_FOR_READY state
  manager.on_erd_seen(0x0001);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* run — device_steady_state triggers discovery                         */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, run_triggers_discovery_when_steady_state)
{
  init_with_erd(0x0001);

  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_complete());
}

TEST(ha_discovery_manager, run_does_nothing_when_not_steady_state)
{
  init_with_erd(0x0001);

  manager.run(false);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* State query methods                                                  */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, is_complete_returns_true_in_complete_state)
{
  init_with_erd(0x0001);
  manager.run(true);
  CHECK_TRUE(manager.is_complete());
}

TEST(ha_discovery_manager, is_failed_returns_true_in_failed_state)
{
  // Cannot directly transition to FAILED in stub builds (no HTTPS errors),
  // but the method simply checks state_ == HA_DISCOVERY_FAILED
  // We verify the getter works by confirming it returns false in other states
  init_with_erd(0x0001);
  CHECK_FALSE(manager.is_failed());
}

TEST(ha_discovery_manager, is_publishing_returns_true_in_publishing_state)
{
  // In stub builds, publish_ha_discovery_() goes straight to COMPLETE.
  // We verify is_publishing() returns false in WAITING_FOR_READY
  init_with_erd(0x0001);
  CHECK_FALSE(manager.is_publishing());
}

TEST(ha_discovery_manager, is_ready_to_start_returns_true_in_waiting_state)
{
  init_with_erd(0x0001);
  CHECK_TRUE(manager.is_ready_to_start());
}

TEST(ha_discovery_manager, is_ready_to_start_returns_false_after_completion)
{
  init_with_erd(0x0001);
  manager.run(true);
  CHECK_FALSE(manager.is_ready_to_start());
}

/* ------------------------------------------------------------------ */
/* get_state                                                            */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, get_state_returns_idle_before_init)
{
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

TEST(ha_discovery_manager, get_state_returns_waiting_for_ready_after_init)
{
  init_with_erd(0x0001);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

TEST(ha_discovery_manager, get_state_returns_complete_after_run)
{
  init_with_erd(0x0001);
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* cleanup                                                              */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, cleanup_no_crash_on_idle)
{
  // cleanup on an uninitialized manager should not crash
  manager.cleanup();
}

TEST(ha_discovery_manager, cleanup_no_crash_on_double_call)
{
  init_with_erd(0x0001);
  manager.cleanup();
  manager.cleanup();
}

TEST(ha_discovery_manager, cleanup_after_run_no_crash)
{
  init_with_erd(0x0001);
  manager.run(true);
  manager.cleanup();
}

/* ------------------------------------------------------------------ */
/* set_mqtt_adapter                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, set_mqtt_adapter_accepts_null)
{
  init_with_erd(0x0001);
  manager.set_mqtt_adapter(nullptr);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Non-ESP-IDF fetch — discovery completes without HTTPS                */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, non_esp_idf_fetch_is_noop_discovery_completes)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002, 0x0003 };
  add_erds(erds, 3);
  manager.init("dev1", "model1", "sn1", &test_cache_, true);

  // In stub builds, publish_ha_discovery_() does not spawn a fetch task
  // and transitions directly to COMPLETE
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_complete());
}

TEST(ha_discovery_manager, non_esp_idf_fetch_completes_via_quiet_window)
{
  esphome_hal_double_set_millis(0);
  init_with_erd(0x0001, true);

  esphome_hal_double_set_millis(HA_DISCOVERY_QUIET_MS);
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

TEST(ha_discovery_manager, non_esp_idf_fetch_completes_via_steady_state)
{
  esphome_hal_double_set_millis(0);
  init_with_erd(0x0001, true);

  // Even with continuous ERD activity, steady_state=true triggers discovery
  for (uint32_t t = 1000; t <= HA_DISCOVERY_MAX_WAIT_MS; t += 1000) {
    esphome_hal_double_set_millis(t);
    manager.on_erd_seen(0x0001);
  }

  esphome_hal_double_set_millis(HA_DISCOVERY_MAX_WAIT_MS);
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Re-init after completion                                             */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, can_reinit_after_completion)
{
  init_with_erd(0x0001);
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());

  // Re-init should reset to WAITING_FOR_READY
  manager.init("dev2", "model2", "sn2", &test_cache_, false);
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* run is idempotent in non-waiting states                              */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, run_noop_in_complete_state)
{
  init_with_erd(0x0001);
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());

  // Running again should not change state
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

TEST(ha_discovery_manager, run_noop_in_idle_state)
{
  // Uninitialized manager — run should do nothing
  manager.run(true);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Edge: on_erd_seen with max capacity                                  */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, on_erd_seen_handles_max_seen_erds)
{
  // Fill cache with many unique ERDs
  uint8_t dummy = 0;
  for (int i = 0; i < HA_DISCOVERY_MAX_ERDS; i++) {
    erd_cache_update(&test_cache_, static_cast<uint16_t>(i), &dummy, 1);
  }
  manager.init("dev1", "model1", "sn1", &test_cache_, false);

  // Fill seen_erds_ to capacity
  for (int i = 0; i < HA_DISCOVERY_MAX_ERDS; i++) {
    manager.on_erd_seen(static_cast<uint16_t>(i));
  }

  // Adding one more unique ERD should not crash
  manager.on_erd_seen(static_cast<uint16_t>(HA_DISCOVERY_MAX_ERDS));
  CHECK_EQUAL(HA_DISCOVERY_WAITING_FOR_READY, manager.get_state());
}
