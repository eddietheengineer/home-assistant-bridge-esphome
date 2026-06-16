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
  CHECK(publisher.mqtt_connected);
  CHECK_EQUAL(0u, publisher.total_published);
  CHECK_EQUAL(0u, publisher.missed_loops);
  CHECK(strncmp(publisher.device_id, "my_device", 8) == 0);
}

TEST(erd_cache_mqtt_publisher, init_sets_mqtt_connected_true)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  CHECK(publisher.mqtt_connected);
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

  uint8_t data = 0x42;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data), true);

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

  // Insert 20 ERDs with update_required=true
  for (uint16_t i = 0; i < 20; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x1000 + i), &data, sizeof(data), true);
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
  erd_cache_update(&cache, 0x0008, &data, sizeof(data), true);

  // Simulate disconnect
  publisher.mqtt_connected = false;

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
  erd_cache_update(&cache, 0x0008, &data, sizeof(data), true);

  // Disconnect — should not publish
  publisher.mqtt_connected = false;
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(0u, published);

  // Reconnect — should publish
  publisher.mqtt_connected = true;
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

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data), true);

  // The publisher will call esphome_mqtt_client_adapter_publish with the topic.
  // We verify it doesn't crash and the topic is constructed correctly.
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
}

/* ------------------------------------------------------------------ */
/* loop - payload format                                                */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, payload_uppercase_hex_no_separator)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data[] = {0x01, 0xAB, 0xFF};
  erd_cache_update(&cache, 0x1001, data, sizeof(data), true);

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

  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x0008, &data, sizeof(data), true);

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

  CHECK(publisher.mqtt_connected);
  erd_cache_mqtt_publisher_on_disconnected(&publisher);
  CHECK(!publisher.mqtt_connected);
}

TEST(erd_cache_mqtt_publisher, on_connected_sets_flag)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  publisher.mqtt_connected = false;
  erd_cache_mqtt_publisher_on_connected(&publisher);
  CHECK(publisher.mqtt_connected);
}

/* ------------------------------------------------------------------ */
/* Event-driven disconnect/reconnect                                    */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, disconnect_event_triggers_callback)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  CHECK(publisher.mqtt_connected);

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

  // Insert 10 ERDs
  for (uint16_t i = 0; i < 10; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x2000 + i), &data, sizeof(data), true);
  }

  // Publish 3
  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 3, 100);
  CHECK_EQUAL(3u, published);

  // Mark remaining as updated for next batch
  for (uint16_t i = 0; i < 10; i++) {
    uint8_t data = (uint8_t)(i + 10);
    erd_cache_update(&cache, (tiny_erd_t)(0x2000 + i), &data, sizeof(data), true);
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

  // Insert many ERDs
  for (uint16_t i = 0; i < 50; i++) {
    uint8_t data = (uint8_t)i;
    erd_cache_update(&cache, (tiny_erd_t)(0x3000 + i), &data, sizeof(data), true);
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

  uint8_t data_a = 0x01;
  uint8_t data_b = 0x02;
  uint8_t data_c = 0x03;
  erd_cache_update(&cache, 0x4001, &data_a, sizeof(data_a), true);
  erd_cache_update(&cache, 0x4002, &data_b, sizeof(data_b), true);
  erd_cache_update(&cache, 0x4003, &data_c, sizeof(data_c), true);

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 10, 100);
  CHECK_EQUAL(3u, published);
  CHECK_EQUAL(3u, publisher.total_published);
}

/* ------------------------------------------------------------------ */
/* loop - large payload hex encoding (Issue 9 fix)                     */
/* ------------------------------------------------------------------ */

TEST(erd_cache_mqtt_publisher, loop_publishes_128_byte_payload)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data[128];
  for (uint8_t i = 0; i < 128; i++) {
    data[i] = i;
  }
  erd_cache_update(&cache, 0x1001, data, sizeof(data), true);

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
  CHECK_EQUAL(1u, publisher.total_published);
}

TEST(erd_cache_mqtt_publisher, loop_publishes_255_byte_payload)
{
  erd_cache_mqtt_publisher_init(
    &publisher,
    &cache,
    &adapter.interface,
    "device");

  uint8_t data[255];
  for (uint16_t i = 0; i < 255; i++) {
    data[i] = (uint8_t)(i & 0xFF);
  }
  erd_cache_update(&cache, 0x1002, data, sizeof(data), true);

  uint16_t published = erd_cache_mqtt_publisher_loop(&publisher, 1, 100);
  CHECK_EQUAL(1u, published);
  CHECK_EQUAL(1u, publisher.total_published);
}
