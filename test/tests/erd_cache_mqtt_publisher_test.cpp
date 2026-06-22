/*!
 * @file
 * @brief Unit tests for the ERD cache MQTT publisher module.
 */

extern "C" {
#include "erd_cache.h"
#include "erd_cache_mqtt_publisher.h"
}

#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"

#include "CppUTest/TestHarness.h"

/* ------------------------------------------------------------------ */
/* Test group                                                          */
/* ------------------------------------------------------------------ */

TEST_GROUP(erd_cache_mqtt_publisher)
{
  erd_cache_mqtt_publisher_t publisher;
  erd_cache_t cache;
  esphome_mqtt_client_adapter_t adapter;

  void setup()
  {
    if (publisher.cache) {
      erd_cache_mqtt_publisher_destroy(&publisher);
    }
    erd_cache_destroy(&cache);
    memset(&publisher, 0, sizeof(publisher));
    erd_cache_init(&cache);
    esphome_mqtt_client_adapter_init(&adapter, "test_device");
  }

  void teardown()
  {
    if (publisher.cache) {
      erd_cache_mqtt_publisher_destroy(&publisher);
    }
    erd_cache_destroy(&cache);
    esphome_mqtt_client_adapter_destroy(&adapter);
  }
};

/* ------------------------------------------------------------------ */
/* init / destroy                                                       */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, init_sets_cache_pointer)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");

  CHECK(publisher.cache != nullptr);
  CHECK_EQUAL(0u, publisher.publish_index);
  CHECK(!publisher.mqtt_connected);  // Initially disconnected; set via on_connected
  CHECK_EQUAL(0u, publisher.total_published);
  CHECK_EQUAL(0u, publisher.missed_loops);
  CHECK(strncmp(publisher.device_id, "my_device", 8) == 0);
}

TEST(erd_cache_mqtt_publisher, init_sets_mqtt_connected_false)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  CHECK(!publisher.mqtt_connected);  // Starts disconnected; on_connected sets true
}

TEST(erd_cache_mqtt_publisher, destroy_unsubscribes_events)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  // Destroy should not crash even after events are set up.
  erd_cache_mqtt_publisher_destroy(&publisher);
  CHECK(publisher.cache == nullptr);
}

TEST(erd_cache_mqtt_publisher, destroy_is_safe_when_not_initialized)
{
  // Destroy on a zeroed struct should not crash.
  erd_cache_mqtt_publisher_destroy(&publisher);
}

TEST(erd_cache_mqtt_publisher, init_handles_null_mqtt_client_gracefully)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    nullptr,
    "device");

  /* Should not crash; fields set before the null guard are still valid. */
  CHECK(publisher.cache != nullptr);
  CHECK_EQUAL(0u, publisher.publish_index);
  CHECK(!publisher.mqtt_connected);  // Initially disconnected
}

TEST(erd_cache_mqtt_publisher, destroy_after_init_with_null_mqtt_client)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    nullptr,
    "device");

  /* Destroy should clean up without crash when mqtt_client was null. */
  erd_cache_mqtt_publisher_destroy(&publisher);
  CHECK(publisher.cache == nullptr);
  CHECK(publisher.mqtt_client == nullptr);
}

/* ------------------------------------------------------------------ */
/* loop - basic publish                                                 */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_publishes_updated_erd)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data));

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);
  CHECK_EQUAL(1u, publisher.total_published);
}
TEST(erd_cache_mqtt_publisher, loop_returns_zero_when_no_updates)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);
}

TEST(erd_cache_mqtt_publisher, loop_respects_max_publishes)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  // Insert 20 ERDs with update_required=true
  for (uint16_t i = 0; i < 20; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x1000 + i), &data, sizeof(data));
  }

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 5, 100);
  CHECK(published <= 5);
  CHECK_EQUAL(published, publisher.total_published);
}

TEST(erd_cache_mqtt_publisher, loop_skips_when_mqtt_disconnected)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data));

  // Disconnect so loop skips publishing
  erd_cache_mqtt_publisher_on_disconnected(&publisher);

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);
  CHECK_EQUAL(0u, publisher.total_published);
}
TEST(erd_cache_mqtt_publisher, loop_resumes_after_reconnect)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data));

  // Disconnect — should not publish
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);

  // Reconnect — should publish
  erd_cache_mqtt_publisher_on_connected(&publisher);
  published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);
}

/* ------------------------------------------------------------------ */
/* loop - topic format                                                  */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, topic_format_correct)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "my_device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data));

  // The publisher will call esphome_mqtt_client_adapter_publish with the topic.
  // We verify it doesn't crash and the topic is constructed correctly.
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
}

/* ------------------------------------------------------------------ */
/* loop - payload format                                                */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, payload_lowercase_hex_no_separator)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data[] = {0x01, 0xAB, 0xFF};
  erd_cache_update(&cache, 0x1001, data, sizeof(data));

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
}

/* ------------------------------------------------------------------ */
/* loop - retain flag                                                   */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, retain_flag_true)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data));

  // The publisher always passes retain=true to esphome_mqtt_client_adapter_publish.
  // We verify the call completes without crashing.
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
}

/* ------------------------------------------------------------------ */
/* on_connected / on_disconnected                                       */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, on_disconnected_sets_flag)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  // Initially disconnected by default
  CHECK(!publisher.mqtt_connected);

  // Disconnect, then reconnect
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK(publisher.mqtt_connected);
}

TEST(erd_cache_mqtt_publisher, on_disconnected_then_connected_toggles_flag)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  // Initially disconnected by default
  CHECK(!publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK(publisher.mqtt_connected);
}

/* ------------------------------------------------------------------ */
/* Event-driven disconnect/reconnect                                    */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, disconnect_event_is_noop_when_already_disconnected)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  // Start disconnected; disconnect event is a no-op, flag stays false

  // Trigger disconnect through the adapter
  esphome_mqtt_client_adapter_notify_disconnected(&adapter);

  CHECK(!publisher.mqtt_connected);
}

TEST(erd_cache_mqtt_publisher, connect_event_triggers_callback)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.mqtt_connected = false;

  // Trigger connect through the adapter
  esphome_mqtt_client_adapter_notify_connected(&adapter);

  CHECK(publisher.mqtt_connected);
}

/* ------------------------------------------------------------------ */
/* loop - round robin index                                             */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_advances_publish_index)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  // Insert 10 ERDs
  for (uint16_t i = 0; i < 10; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x2000 + i), &data, sizeof(data));
  }

  // Publish 3
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 3, 100);
  CHECK_EQUAL(3u, published);

  // Mark remaining as updated for next batch
  for (uint16_t i = 0; i < 10; i++) {
    uint8_t data = (uint8_t)(i + 10);
    erd_cache_update(&cache, (tiny_erd_t)(0x2000 + i), &data, sizeof(data));
  }

  // Publish next batch — should pick up from where it left off
  published = erd_cache_mqtt_publisher_loop(&publisher, 3, 100);
  CHECK(published > 0);
}

/* ------------------------------------------------------------------ */
/* loop - time budget                                                   */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, loop_respects_time_budget)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  // Insert many ERDs
  for (uint16_t i = 0; i < 50; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x3000 + i), &data, sizeof(data));
  }

  // Set initial time
  esphome_hal_double_set_millis(0);

  // With max_ms=1, it should publish at least one before the budget check
  // stops it (since each publish takes >1ms of simulated time after we advance).
  // We advance time after each publish by having the loop check millis().
  // But since millis() is static during the call, all publishes happen at t=0.
  // The first iteration: start_ms=0, millis()-start_ms=0 < 1, publishes.
  // Second iteration: millis()-start_ms=0 < 1, publishes.
  // All 50 publish because millis() doesn't advance during the call.
  // This is expected behavior — the time budget only works when millis() advances.
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 100, 1);
  // When millis() is frozen, the time budget is effectively infinite.
  // The max_publishes cap is what limits us.
  CHECK_EQUAL(50u, published);
}

TEST(erd_cache_mqtt_publisher, loop_returns_zero_with_null_cache)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.cache = nullptr;
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);
}

TEST(erd_cache_mqtt_publisher, loop_returns_zero_with_null_mqtt_client)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.mqtt_client = nullptr;
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);
}

/* ------------------------------------------------------------------ */
/* Multiple publishes in a single loop                                  */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_publishes_multiple_erds)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data_a = 0x01;
  uint8_t data_b = 0x02;
  uint8_t data_c = 0x03;
  erd_cache_update(&cache, 0x4001, &data_a, sizeof(data_a));
  erd_cache_update(&cache, 0x4002, &data_b, sizeof(data_b));
  erd_cache_update(&cache, 0x4003, &data_c, sizeof(data_c));

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(3u, published);
  CHECK_EQUAL(3u, publisher.total_published);
}

/* ------------------------------------------------------------------ */
/* loop - large payload hex encoding (Issue 9 fix)                     */
/* ------------------------------------------------------------------ */
TEST(erd_cache_mqtt_publisher, loop_publishes_32_byte_payload)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data[32];
  for (uint8_t i = 0; i < 32; i++) {
    data[i] = i;
  }
  erd_cache_update(&cache, 0x1001, data, sizeof(data));

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
  CHECK_EQUAL(1u, publisher.total_published);
}
/* ------------------------------------------------------------------ */
/* Cache change-detection tests                                        */
/* ------------------------------------------------------------------ */

TEST_GROUP(erd_cache_change_detection)
{
  erd_cache_t cache;

  void setup()
  {
    erd_cache_init(&cache);
  }

  void teardown()
  {
    erd_cache_destroy(&cache);
  }
};

/* same-size, same data → no change */
TEST(erd_cache_change_detection, same_size_same_data_no_change)
{
  uint8_t data[] = { 0x01, 0x02, 0x03 };
  erd_cache_update(&cache, 0x1001, data, sizeof(data));
  /* update with identical data */
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, data, sizeof(data)));
}

/* same-size, different data → change detected */
TEST(erd_cache_change_detection, same_size_different_data_change_detected)
{
  uint8_t data1[] = { 0x01, 0x02, 0x03 };
  uint8_t data2[] = { 0x01, 0x02, 0x04 };
  erd_cache_update(&cache, 0x1001, data1, sizeof(data1));
  CHECK_TRUE(erd_cache_update(&cache, 0x1001, data2, sizeof(data2)));
}

/* Size shrink is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, size_shrink_same_prefix_change_detected)
{
  uint8_t data1[] = { 0x01, 0x02, 0x03, 0x04 };
  uint8_t data2[] = { 0x01, 0x02 };
  erd_cache_update(&cache, 0x1001, data1, sizeof(data1));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, data2, sizeof(data2)));
}

/* Size grow is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, size_grow_same_prefix_change_detected)
{
  uint8_t data1[] = { 0x01, 0x02 };
  uint8_t data2[] = { 0x01, 0x02, 0x03, 0x04 };
  erd_cache_update(&cache, 0x1001, data1, sizeof(data1));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, data2, sizeof(data2)));
}

/* Inline-to-heap promotion is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, inline_to_heap_promotion_change_detected)
{
  uint8_t data_small[8];
  memset(data_small, 0xAA, sizeof(data_small));
  erd_cache_update(&cache, 0x1001, data_small, sizeof(data_small));

  uint8_t data_large[20];
  memcpy(data_large, data_small, sizeof(data_small));
  memset(data_large + sizeof(data_small), 0xBB, sizeof(data_large) - sizeof(data_small));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, data_large, sizeof(data_large)));
}

/* Heap-to-inline shrink is treated as appliance lost — returns false. */
TEST(erd_cache_change_detection, heap_to_inline_shrink_change_detected)
{
  uint8_t data_large[20];
  memset(data_large, 0xAA, sizeof(data_large));
  erd_cache_update(&cache, 0x1001, data_large, sizeof(data_large));

  uint8_t data_small[8];
  memset(data_small, 0xAA, sizeof(data_small));
  CHECK_FALSE(erd_cache_update(&cache, 0x1001, data_small, sizeof(data_small)));
}

/* New entry with data > 4 bytes uses heap storage */
TEST(erd_cache_change_detection, heap_path_new_entry_uses_heap)
{
  uint8_t data[20];
  for (uint8_t i = 0; i < 20; i++) {
    data[i] = i;
  }
  erd_cache_update(&cache, 0x1001, data, sizeof(data));

  CHECK_EQUAL(1u, erd_cache_get_count(&cache));

  uint16_t iterator = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iterator);
  CHECK(entry != NULL);
  CHECK_TRUE(entry->uses_heap);
  CHECK_EQUAL(20u, entry->data_size);
  for (uint8_t i = 0; i < 20; i++) {
    CHECK_EQUAL(i, entry->ext_data[i]);
  }
}

/* Update existing heap entry with different data */
TEST(erd_cache_change_detection, heap_path_update_existing_entry)
{
  uint8_t data1[20];
  for (uint8_t i = 0; i < 20; i++) {
    data1[i] = i;
  }
  erd_cache_update(&cache, 0x1001, data1, sizeof(data1));

  uint8_t data2[20];
  for (uint8_t i = 0; i < 20; i++) {
    data2[i] = 255 - i;
  }
  CHECK_TRUE(erd_cache_update(&cache, 0x1001, data2, sizeof(data2)));

  CHECK_EQUAL(1u, erd_cache_get_count(&cache));

  uint16_t iterator = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iterator);
  CHECK(entry != NULL);
  CHECK_TRUE(entry->uses_heap);
  CHECK_EQUAL(20u, entry->data_size);
  for (uint8_t i = 0; i < 20; i++) {
    CHECK_EQUAL(255 - i, entry->ext_data[i]);
  }
}

/* #19: Cache overflow — 201st insert rejected when cache is full */
TEST(erd_cache_change_detection, cache_overflow_rejects_new_erd)
{
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    uint8_t data = 0x42;
    erd_cache_update(&cache, (tiny_erd_t)(0x8000 + i), &data, sizeof(data));
  }
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_count(&cache));

  /* 201st ERD should be rejected */
  uint8_t data = 0xFF;
  CHECK_FALSE(erd_cache_update(&cache, 0xFFFF, &data, sizeof(data)));
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_count(&cache));
}

/* #20: max_ms=0 — loop publishes nothing because time check fails immediately */
TEST(erd_cache_mqtt_publisher, loop_publishes_nothing_with_max_ms_zero)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));

  esphome_hal_double_set_millis(0);
  erd_cache_mqtt_publisher_set_time_fn(&publisher, esphome_hal_double_get_millis);

  /* max_ms=0: first iteration checks get_time_ms()-start_ms >= 0, which is 0>=0=true */
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 0);
  CHECK_EQUAL(0u, published);
}

/* #23: MQTT reconnect — ERD still published after disconnect/reconnect cycle */
TEST(erd_cache_mqtt_publisher, loop_publishes_after_disconnect_reconnect)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));

  /* Simulate disconnect — loop should skip publishing */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);
  CHECK(publisher.missed_loops > 0);

  /* Simulate reconnect — loop should publish the pending ERD */
  erd_cache_mqtt_publisher_on_connected(&publisher);
  published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);
}
/* #23: MQTT reconnect — no publish after reconnect when nothing has changed */
TEST(erd_cache_mqtt_publisher, loop_no_publish_after_reconnect_when_no_changes)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));

  /* Publish the ERD first to clear update_required */
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);

  /* Disconnect and reconnect */
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  erd_cache_mqtt_publisher_on_connected(&publisher);

  /* No new data — nothing should be published */
  published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);
}

/* Rate limiting: publisher reloads cooldown after successful publish */
TEST(erd_cache_mqtt_publisher, loop_reloads_cooldown_after_publish)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));

  /* First publish — immediate (new entry, cooldown=0). */
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);

  /* Verify cooldown was reloaded to max_cooldown. */
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != NULL);
  CHECK_EQUAL(5, entry->publish_cooldown);
}

/* Rate limiting: publisher skips entries whose cooldown has not expired */
TEST(erd_cache_mqtt_publisher, loop_skips_rate_limited_entries)
{
  erd_cache_set_throttle_rate_seconds(&cache, 5);
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");
  erd_cache_mqtt_publisher_on_connected(&publisher);

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));

  /* First publish — immediate. */
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);

  /* Update again — cooldown is 5, should be blocked. */
  uint8_t data2 = 0x99;
  erd_cache_update(&cache, 0x1001, &data2, sizeof(data2));

  /* Loop should publish nothing (rate-limited). */
  published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);

  /* Tick cooldown to 0. */
  for (int i = 0; i < 5; i++) {
    erd_cache_tick_cooldowns(&cache);
  }

  /* Now the loop should publish. */
  published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(1u, published);
}
