/*!
 * @file
 * @brief Integration test: MQTT write topic -> adapter -> write bridge -> ERD client.
 *
 * Verifies the complete path from an incoming MQTT write message through the
 * adapter's wildcard subscription, into the write bridge, and finally to the
 * ERD client's write() call.
 */

extern "C" {
#include "erd_write_bridge.h"
#include "erd_cache.h"
#include "tiny_gea_constants.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_mqtt_client_adapter.h"
#include "double/mqtt_test_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"
#include "double/esphome_hal_double.hpp"
#include "erd_registry.h"

#include <cstdio>
#include <string>

TEST_GROUP(write_flow_integration)
{
  esphome_mqtt_client_adapter_t adapter;
  erd_write_bridge_t write_bridge;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  esphome::geappliances_bridge::ErdRegistry erd_registry;

  esphome::mqtt::MqttTestDouble mqtt_double;

  void setup()
  {
    mock().strictOrder();
    esphome_hal_double_set_millis(0);

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);

    esphome::mqtt::global_mqtt_client = &mqtt_double;
    mqtt_double.connected_ = true;
  }

  void teardown()
  {
    esphome::mqtt::global_mqtt_client = nullptr;
    mqtt_double.connected_ = false;

    erd_write_bridge_destroy(&write_bridge);
    erd_cache_destroy(&test_cache);

    esphome_mqtt_client_adapter_destroy(&adapter);
    mock().clear();
  }

  void init_all(uint8_t host_address)
  {
    mock().disable();

    esphome_mqtt_client_adapter_init(&adapter, "test_device");
    esphome_mqtt_client_adapter_set_erd_registry(&adapter, &erd_registry);

    erd_write_bridge_init(
      &write_bridge,
      &timer_group.timer_group,
      &erd_client.interface,
      &adapter.interface,
      host_address,
      &test_cache);

    esphome_mqtt_client_adapter_subscribe_write_topic(&adapter);

    mock().enable();
  }

  void when_home_assistant_publishes_write(tiny_erd_t erd, const std::string& payload)
  {
    char topic[128];
    snprintf(topic, sizeof(topic), "geappliances/test_device/erd/0x%04x/write", erd);
    mqtt_double.simulate_message(topic, payload);
  }
};

TEST(write_flow_integration, should_route_write_from_mqtt_to_erd_client)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");
}

TEST(write_flow_integration, should_parse_erd_from_topic_path)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0xFF;
  tiny_erd_t erd = 0x0092;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "FF");
}

TEST(write_flow_integration, should_drop_write_when_host_is_broadcast)
{
  init_all(tiny_gea_broadcast_address);

  tiny_erd_t erd = 0x7701;

  when_home_assistant_publishes_write(erd, "01");

  // The dropped write is reported on the unprefixed result topic.
  CHECK_EQUAL(std::string("geappliances/test_device/erd/0x7701/write_result"),
    mqtt_double.last_published_topic_);
  CHECK_EQUAL(std::string("{\"error\":\"not_supported\"}"),
    mqtt_double.last_published_payload_);
}

TEST(write_flow_integration, should_report_write_result_on_completion)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");

  // Simulate write completion.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.address = host;
  args.write_completed.request_id = mock_request_id;
  args.write_completed.erd = erd;
  args.write_completed.data = &dummy;
  args.write_completed.data_size = 1;

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  // The result is published to the unprefixed topic (write went to the
  // detected host).
  CHECK_EQUAL(std::string("geappliances/test_device/erd/0x7701/write_result"),
    mqtt_double.last_published_topic_);
  CHECK_EQUAL(std::string("ok"), mqtt_double.last_published_payload_);
}

TEST(write_flow_integration, should_report_write_failure_to_mqtt)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", host)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");

  // Simulate write failure.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_failed;
  args.address = host;
  args.write_failed.request_id = mock_request_id;
  args.write_failed.erd = erd;
  args.write_failed.data = &dummy;
  args.write_failed.data_size = 1;
  args.write_failed.reason = tiny_gea3_erd_client_write_failure_reason_incorrect_size;

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  CHECK_EQUAL(std::string("geappliances/test_device/erd/0x7701/write_result"),
    mqtt_double.last_published_topic_);
  CHECK_EQUAL(std::string("{\"error\":\"incorrect_size\"}"),
    mqtt_double.last_published_payload_);
}

TEST(write_flow_integration, should_route_write_to_secondary_board_from_cache)
{
  uint8_t host = 0xC0;
  init_all(host);

  // The ERD is cached on a secondary board (not the primary host).
  uint8_t cached_data = 0x00;
  erd_cache_update(&test_cache, 0x7701, 0x12, &cached_data, 1);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", 0x12)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  when_home_assistant_publishes_write(erd, "01");

  // Simulate write completion.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.address = 0x12;
  args.write_completed.request_id = mock_request_id;
  args.write_completed.erd = erd;
  args.write_completed.data = &dummy;
  args.write_completed.data_size = 1;

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  // The result mirrors the per-board topic the write was routed to.
  CHECK_EQUAL(std::string("geappliances/test_device/erd/0x12_0x7701/write_result"),
    mqtt_double.last_published_topic_);
  CHECK_EQUAL(std::string("ok"), mqtt_double.last_published_payload_);
}

TEST(write_flow_integration, should_route_write_to_secondary_board_from_topic)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0x7701;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", 0x12)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  // Per-board write topic: the board address in the topic names the target.
  mqtt_double.simulate_message("geappliances/test_device/erd/0x12_0x7701/write", "01");

  // Simulate write completion.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.address = 0x12;
  args.write_completed.request_id = mock_request_id;
  args.write_completed.erd = erd;
  args.write_completed.data = &dummy;
  args.write_completed.data_size = 1;

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  // The result mirrors the per-board topic the write was routed to.
  CHECK_EQUAL(std::string("geappliances/test_device/erd/0x12_0x7701/write_result"),
    mqtt_double.last_published_topic_);
  CHECK_EQUAL(std::string("ok"), mqtt_double.last_published_payload_);
}

TEST(write_flow_integration, should_route_write_to_board_0x62_erd_0xf408_from_field_report_topic)
{
  uint8_t host = 0xC0;
  init_all(host);

  uint8_t value = 0x01;
  tiny_erd_t erd = 0xf408;
  tiny_gea3_erd_client_request_id_t mock_request_id{1};

  mock()
    .expectOneCall("write")
    .onObject(&erd_client)
    .withParameter("address", 0x62)
    .withParameter("erd", erd)
    .withMemoryBufferParameter("data", &value, 1)
    .withOutputParameterReturning("request_id", &mock_request_id, sizeof(mock_request_id))
    .andReturnValue(true);

  // Exact topic from the field report: per-board write for board 0x62, ERD 0xf408.
  // The device ID in the topic is irrelevant to parsing (the wildcard subscription
  // matches it); the segment after "erd/" is what routes the write.
  mqtt_double.simulate_message("geappliances/WaterHeater_PF80S10FPY01_ZA601071X/erd/0x62_0xf408/write", "01");

  // Simulate write completion.
  uint8_t dummy = 0;
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_write_completed;
  args.address = 0x62;
  args.write_completed.request_id = mock_request_id;
  args.write_completed.erd = erd;
  args.write_completed.data = &dummy;
  args.write_completed.data_size = 1;

  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);

  // The result mirrors the per-board topic the write was routed to.
  CHECK_EQUAL(std::string("geappliances/test_device/erd/0x62_0xf408/write_result"),
    mqtt_double.last_published_topic_);
  CHECK_EQUAL(std::string("ok"), mqtt_double.last_published_payload_);
}
