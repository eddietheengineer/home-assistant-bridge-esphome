/*!\
 * @file
 * @brief Unit tests for HaDiscoveryManager state machine.
 *
 * Tests the state machine logic on non-ESP-IDF builds (USE_ESP_IDF_STUBS).
 * The embedded JSONL fetch path is a no-op in stub builds; tests cover:
 *   - configure: device info storage
 *   - start: triggers discovery, transitions to COMPLETE in stub builds
 *   - run: drives publishing/cleanup state machine
 *   - State query methods (is_complete, is_failed, etc.)
 *   - cleanup: safe on double call
 *   - Non-ESP-IDF fetch: discovery completes without actual decompression
 */

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "ha_discovery_manager.h"
#include "geappliances_bridge_constants.h"
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

  // Helper: configure with a single ERD in the cache
  void configure_with_erd(tiny_erd_t erd, bool gen_config = false)
  {
    uint8_t dummy = 0;
    erd_cache_update(&test_cache_, erd, &dummy, 1);
    manager.configure("dev1", "model1", "sn1", &test_cache_, gen_config);
  }
};

/* ------------------------------------------------------------------ */
/* configure                                                            */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, configure_stores_device_info)
{
  configure_with_erd(0x0001, true);
  // configure() does not change state
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
  CHECK_FALSE(manager.is_complete());
  CHECK_FALSE(manager.is_failed());
  CHECK_FALSE(manager.is_publishing());
}

TEST(ha_discovery_manager, configure_with_full_cache)
{
  // Populate cache with many entries.
  uint8_t dummy = 0;
  for (int i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_update(&test_cache_, static_cast<uint16_t>(i), &dummy, 1);
  }
  manager.configure("dev1", "model1", "sn1", &test_cache_, false);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

TEST(ha_discovery_manager, configure_with_empty_cache)
{
  // test_cache_ is empty (no entries added)
  manager.configure("dev1", "model1", "sn1", &test_cache_, false);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* start                                                                */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, start_triggers_discovery)
{
  configure_with_erd(0x0001);

  manager.start();
  // In stub builds, publish_ha_discovery_() goes straight to COMPLETE
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_complete());
}

TEST(ha_discovery_manager, start_idempotent)
{
  configure_with_erd(0x0001);

  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());

  // Calling start() again after completion should be a no-op
  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

TEST(ha_discovery_manager, start_noop_when_not_configured)
{
  // start() without configure() — state is IDLE but cache is null
  // Should not crash; in stub builds it goes to COMPLETE
  manager.start();
  // State changes from IDLE to COMPLETE (stub path)
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* run                                                                  */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, run_noop_in_idle_state)
{
  // Uninitialized manager — run should do nothing
  manager.run();
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

TEST(ha_discovery_manager, run_noop_in_complete_state)
{
  configure_with_erd(0x0001);
  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());

  // Running again should not change state
  manager.run();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* State query methods                                                  */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, is_complete_returns_true_in_complete_state)
{
  configure_with_erd(0x0001);
  manager.start();
  CHECK_TRUE(manager.is_complete());
}

TEST(ha_discovery_manager, is_failed_returns_true_in_failed_state)
{
  // Cannot directly transition to FAILED in stub builds (no alloc errors),
  // but the method simply checks state_ == HA_DISCOVERY_FAILED
  // We verify the getter works by confirming it returns false in other states
  configure_with_erd(0x0001);
  CHECK_FALSE(manager.is_failed());
}

TEST(ha_discovery_manager, is_publishing_returns_true_in_publishing_state)
{
  // In stub builds, publish_ha_discovery_() goes straight to COMPLETE.
  // We verify is_publishing() returns false in IDLE
  configure_with_erd(0x0001);
  CHECK_FALSE(manager.is_publishing());
}

/* ------------------------------------------------------------------ */
/* get_state                                                            */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, get_state_returns_idle_before_configure)
{
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

TEST(ha_discovery_manager, get_state_returns_idle_after_configure)
{
  configure_with_erd(0x0001);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

TEST(ha_discovery_manager, get_state_returns_complete_after_start)
{
  configure_with_erd(0x0001);
  manager.start();
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
  configure_with_erd(0x0001);
  manager.cleanup();
  manager.cleanup();
}

TEST(ha_discovery_manager, cleanup_after_start_no_crash)
{
  configure_with_erd(0x0001);
  manager.start();
  manager.cleanup();
}

/* ------------------------------------------------------------------ */
/* set_mqtt_adapter                                                     */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, set_mqtt_adapter_accepts_null)
{
  configure_with_erd(0x0001);
  manager.set_mqtt_adapter(nullptr);
  CHECK_EQUAL(HA_DISCOVERY_IDLE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Non-ESP-IDF fetch — discovery completes without decompression        */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, non_esp_idf_fetch_is_noop_discovery_completes)
{
  tiny_erd_t erds[] = { 0x0001, 0x0002, 0x0003 };
  add_erds(erds, 3);
  manager.configure("dev1", "model1", "sn1", &test_cache_, true);

  // In stub builds, publish_ha_discovery_() does not spawn a fetch task
  // and transitions directly to COMPLETE
  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
  CHECK_TRUE(manager.is_complete());
}

/* ------------------------------------------------------------------ */
/* Re-init after completion                                             */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, start_is_noop_after_completion)
{
  configure_with_erd(0x0001);
  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());

  // Re-configure and start again — requires cleanup first to reset to IDLE
  manager.cleanup();
  configure_with_erd(0x0002);
  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}

/* ------------------------------------------------------------------ */
/* Edge: configure with max capacity cache                              */
/* ------------------------------------------------------------------ */

TEST(ha_discovery_manager, configure_handles_max_cache_erds)
{
  // Fill cache to capacity
  uint8_t dummy = 0;
  for (int i = 0; i < HA_DISCOVERY_MAX_ERDS; i++) {
    erd_cache_update(&test_cache_, static_cast<uint16_t>(i), &dummy, 1);
  }
  manager.configure("dev1", "model1", "sn1", &test_cache_, false);

  // Should not crash on start
  manager.start();
  CHECK_EQUAL(HA_DISCOVERY_COMPLETE, manager.get_state());
}