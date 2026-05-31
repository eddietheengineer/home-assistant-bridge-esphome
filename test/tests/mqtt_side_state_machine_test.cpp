/*
 * @file
 * @brief Unit tests for the MqttSideStateMachine class.
 *
 * Validates state transitions, drain behavior, MAX_FLUSH_PER_CALL enforcement,
 * and correct interaction with ErdStateTable and the MQTT adapter.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "mqtt_side_state_machine.h"
#include "erd_state_table.h"
#include "global_state_registry.h"
#include "erd_registry.h"
#include "esphome_mqtt_client_adapter.h"
#include "double/esphome_hal_double.hpp"
#include "esphome/components/mqtt/mqtt_client.h"

#include <string>
#include <vector>
#include <cstring>
#include <functional>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Mock MQTT client that tracks calls (same pattern as adapter tests)  */
/* ------------------------------------------------------------------ */

struct MockMqttClient : public esphome::mqtt::MQTTClientComponent {
  bool connected;
  std::vector<std::string> published_topics;
  std::vector<std::string> published_payloads;
  std::vector<bool> published_retain;
  std::vector<std::string> subscribed_topics;
  std::vector<std::function<void(const std::string&, const std::string&)>> subscribed_callbacks;

  MockMqttClient() : connected(false) {}

  bool is_connected() override { return connected; }

  void publish(const std::string& topic, const std::string& payload,
               uint8_t /*qos*/, bool retain) override
  {
    published_topics.push_back(topic);
    published_payloads.push_back(payload);
    published_retain.push_back(retain);
  }

  void subscribe(const std::string& topic,
                 std::function<void(const std::string&, const std::string&)> callback,
                 uint8_t /*qos*/) override
  {
    subscribed_topics.push_back(topic);
    subscribed_callbacks.push_back(callback);
  }

  void clear()
  {
    subscribed_callbacks.clear();
    published_topics.clear();
    published_payloads.clear();
    published_retain.clear();
    subscribed_topics.clear();
    connected = false;
  }
};

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(mqtt_side_state_machine)
{
  ErdStateTable state_table;
  GlobalStateRegistry registry;
  ErdRegistry erd_registry;
  esphome_mqtt_client_adapter_t adapter;
  MockMqttClient mock_client;
  MqttSideStateMachine* fsm;

  void setup()
  {
    mock().strictOrder();
    registry.set_device_id("test_device");
    esphome::mqtt::global_mqtt_client = &mock_client;
    mock_client.clear();
    esphome_hal_double_set_millis(0);
    fsm = nullptr;
  }

  void teardown()
  {
    delete fsm;
    fsm = nullptr;
    esphome_mqtt_client_adapter_destroy(&adapter);
    esphome::mqtt::global_mqtt_client = nullptr;
    mock().clear();
  }

  void init_adapter()
  {
    esphome_mqtt_client_adapter_init(&adapter, "test_device");
    esphome_mqtt_client_adapter_set_erd_registry(&adapter, &erd_registry);
  }

  void construct_fsm()
  {
    init_adapter();
    fsm = new MqttSideStateMachine(
      &state_table, &registry, &erd_registry, &adapter, nullptr);
  }
};

/* ------------------------------------------------------------------ */
/* Construction and initial state                                       */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, starts_disconnected)
{
  construct_fsm();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* on_mqtt_connected: DISCONNECTED -> SUBSCRIBING                       */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, connected_from_disconnected_goes_to_subscribing)
{
  construct_fsm();
  fsm->on_mqtt_connected();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::SUBSCRIBING),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* loop in SUBSCRIBING: subscribes and transitions to FLUSHING          */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, loop_subscribing_calls_subscribe_and_goes_to_flushing)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING

  mock_client.connected = true;
  fsm->loop();

  CHECK_EQUAL(1u, mock_client.subscribed_topics.size());
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::FLUSHING),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* loop in FLUSHING: drains flagged ERDs                                */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, loop_flushing_publishes_flagged_erds)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  mock_client.connected = true;

  // Add a flagged ERD
  uint8_t value[] = {0x01, 0x02, 0x03};
  state_table.update_erd_value(0x1001, value, 3);

  fsm->loop();

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_topics.back() ==
        "geappliances/test_device/erd/0x1001/value");
  CHECK(mock_client.published_payloads.back() == "010203");
  CHECK_TRUE(mock_client.published_retain.back());
}

/* ------------------------------------------------------------------ */
/* FLUSHING -> RUNNING when no flagged ERDs                             */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, flushing_transitions_to_running_when_empty)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  mock_client.connected = true;

  // No flagged ERDs -- should transition to RUNNING
  fsm->loop();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::RUNNING),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* loop in RUNNING: drains new flagged ERDs                             */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, loop_running_publishes_new_flagged_erds)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING
  fsm->loop();               // -> RUNNING (no flagged ERDs)

  mock_client.connected = true;

  // Add a new flagged ERD while in RUNNING
  uint8_t value[] = {0xAA, 0xBB};
  state_table.update_erd_value(0x2001, value, 2);

  fsm->loop();

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_topics.back() ==
        "geappliances/test_device/erd/0x2001/value");
  CHECK(mock_client.published_payloads.back() == "aabb");
}

/* ------------------------------------------------------------------ */
/* MAX_FLUSH_PER_CALL enforcement                                       */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, flushes_max_per_call)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  mock_client.connected = true;

  // Add 10 flagged ERDs
  for (int i = 0; i < 10; i++) {
    uint8_t value[] = {static_cast<uint8_t>(i)};
    tiny_erd_t erd = static_cast<tiny_erd_t>(0x3000 + i);
    state_table.update_erd_value(erd, value, 1);
  }

  // One loop call should flush at most 5
  fsm->loop();

  CHECK_EQUAL(5u, mock_client.published_topics.size());

  // 5 flags should be cleared, 5 remain
  CHECK_EQUAL(5u, state_table.get_flagged_erds().size());

  // Another loop call flushes the remaining 5
  fsm->loop();

  CHECK_EQUAL(10u, mock_client.published_topics.size());
  CHECK_EQUAL(0u, state_table.get_flagged_erds().size());
}

/* ------------------------------------------------------------------ */
/* on_mqtt_disconnected: any state -> DISCONNECTED                       */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, disconnected_from_running)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING
  fsm->loop();               // -> RUNNING

  fsm->on_mqtt_disconnected();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));
}

TEST(mqtt_side_state_machine, disconnected_from_flushing)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  fsm->on_mqtt_disconnected();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));
}

TEST(mqtt_side_state_machine, disconnected_from_subscribing)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING

  fsm->on_mqtt_disconnected();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* on_mqtt_connected: non-disconnected states -> FLUSHING                */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, connected_from_running_goes_to_flushing)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING
  fsm->loop();               // -> RUNNING

  fsm->on_mqtt_connected();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::FLUSHING),
              static_cast<int>(fsm->get_current_state()));
}

TEST(mqtt_side_state_machine, connected_from_flushing_stays_flushing)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  fsm->on_mqtt_connected();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::FLUSHING),
              static_cast<int>(fsm->get_current_state()));
}

TEST(mqtt_side_state_machine, connected_from_subscribing_goes_to_flushing)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING

  fsm->on_mqtt_connected();

  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::FLUSHING),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* loop in DISCONNECTED: no-op                                          */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, loop_disconnected_is_noop)
{
  construct_fsm();
  // Already DISCONNECTED
  fsm->loop();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));

  // Verify no publishes happened
  CHECK_EQUAL(0u, mock_client.published_topics.size());
}

/* ------------------------------------------------------------------ */
/* Drain stops when adapter disconnects mid-drain                        */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, drain_stops_when_disconnected)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  // Add 3 flagged ERDs
  for (int i = 0; i < 3; i++) {
    uint8_t value[] = {static_cast<uint8_t>(i)};
    tiny_erd_t erd = static_cast<tiny_erd_t>(0x4000 + i);
    state_table.update_erd_value(erd, value, 1);
  }

  // MQTT is disconnected -- drain should do nothing
  mock_client.connected = false;
  fsm->loop();

  // Nothing should have been published
  CHECK_EQUAL(0u, mock_client.published_topics.size());
  // Flags should still be set
  CHECK_EQUAL(3u, state_table.get_flagged_erds().size());
}

/* ------------------------------------------------------------------ */
/* String-type ERD formatting                                           */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, publishes_string_erds_as_ascii)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  mock_client.connected = true;

  // Register ERD as string type
  std::set<tiny_erd_t> string_erds;
  string_erds.insert(0x5001);
  erd_registry.set_string_erds(string_erds);

  uint8_t value[] = "Hello";
  state_table.update_erd_value(0x5001, value, 5);

  fsm->loop();

  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK(mock_client.published_payloads.back() == "Hello");
}

/* ------------------------------------------------------------------ */
/* Full reconnect cycle                                                 */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, full_reconnect_cycle)
{
  construct_fsm();

  // Initial state
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));

  // Connect
  fsm->on_mqtt_connected();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::SUBSCRIBING),
              static_cast<int>(fsm->get_current_state()));

  mock_client.connected = true;
  fsm->loop();  // subscribe + -> FLUSHING
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::FLUSHING),
              static_cast<int>(fsm->get_current_state()));

  // Add some ERDs to flush
  uint8_t v1[] = {0x01};
  state_table.update_erd_value(0x6001, v1, 1);

  fsm->loop();  // flush 1 ERD -> RUNNING
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::RUNNING),
              static_cast<int>(fsm->get_current_state()));

  // Disconnect
  fsm->on_mqtt_disconnected();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::DISCONNECTED),
              static_cast<int>(fsm->get_current_state()));

  // Reconnect — from DISCONNECTED, goes to SUBSCRIBING (not FLUSHING)
  fsm->on_mqtt_connected();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::SUBSCRIBING),
              static_cast<int>(fsm->get_current_state()));

  // Loop subscribes (idempotent) and transitions to FLUSHING
  fsm->loop();
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::FLUSHING),
              static_cast<int>(fsm->get_current_state()));

  // Add more ERDs
  uint8_t v2[] = {0x02};
  state_table.update_erd_value(0x6002, v2, 1);

  fsm->loop();  // flush -> RUNNING
  CHECK_EQUAL(static_cast<int>(MqttSideStateMachine::State::RUNNING),
              static_cast<int>(fsm->get_current_state()));
}

/* ------------------------------------------------------------------ */
/* get_erd_value returns nullptr: skip without clearing flag             */
/* ------------------------------------------------------------------ */

TEST(mqtt_side_state_machine, publishes_and_clears_flag)
{
  construct_fsm();
  fsm->on_mqtt_connected();  // -> SUBSCRIBING
  fsm->loop();               // -> FLUSHING

  mock_client.connected = true;

  uint8_t value[] = {0xFF};
  state_table.update_erd_value(0x7001, value, 1);

  fsm->loop();

  // ERD was published and flag cleared
  CHECK_EQUAL(1u, mock_client.published_topics.size());
  CHECK_EQUAL(0u, state_table.get_flagged_erds().size());
}
