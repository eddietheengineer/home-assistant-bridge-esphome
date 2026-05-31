/*!
 * @file
 * @brief Unit tests for MqttConnectionManager.
 *
 * Validates FSM state transitions, event callbacks, data-bus writes, and
 * adapter interaction under connect/disconnect sequences.
 */

/* Include STL before CppUTest to avoid the operator-new macro clash */
#include <map>
#include <string>
#include <vector>
#include <functional>

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "double/tiny_timer_group_double.hpp"

#include "mqtt_connection_manager.h"
#include "erd_data_bus.h"
#include "erd_data_bus_ids.h"
#include "esphome_mqtt_client_adapter.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "double/esphome_hal_double.hpp"

using namespace esphome::geappliances_bridge;

// ---------------------------------------------------------------------------
// Minimal mock MQTT client (same pattern as esphome_mqtt_client_adapter_test)
// Placed in an anonymous namespace to avoid ODR conflict with other test files.
// ---------------------------------------------------------------------------

namespace {

struct MockMqttClient : public esphome::mqtt::MQTTClientComponent {
  bool connected{false};
  int subscribe_call_count{0};
  int publish_call_count{0};

  bool is_connected() override { return connected; }

  void publish(const std::string& /*topic*/, const std::string& /*payload*/,
               uint8_t /*qos*/, bool /*retain*/) override
  {
    publish_call_count++;
  }

  void subscribe(const std::string& /*topic*/,
                 std::function<void(const std::string&, const std::string&)> /*cb*/,
                 uint8_t /*qos*/) override
  {
    subscribe_call_count++;
  }
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Helper: subscribe to an event and count fires
// ---------------------------------------------------------------------------

struct EventCounter {
  int count{0};
  tiny_event_subscription_t sub{};

  void attach(i_tiny_event_t* event)
  {
    tiny_event_subscription_init(&sub, this, callback);
    tiny_event_subscribe(event, &sub);
  }

  static void callback(void* context, const void* /*args*/)
  {
    static_cast<EventCounter*>(context)->count++;
  }
};

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TEST_GROUP(mqtt_connection_manager)
{
  enum {
    tick_period_ms = 1,
  };

  tiny_timer_group_double_t timer_group;
  ErdDataBus bus;
  MqttConnectionManager mgr;
  esphome_mqtt_client_adapter_t adapter;
  MockMqttClient mock_mqtt;
  bool adapter_initialized{false};
  bool adapter_needs_destroy{false};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    esphome_hal_double_set_millis(0);
    esphome::mqtt::global_mqtt_client = &mock_mqtt;
  }

  void teardown()
  {
    mgr.destroy();
    esphome::mqtt::global_mqtt_client = nullptr;
    if (adapter_needs_destroy) {
      esphome_mqtt_client_adapter_destroy(&adapter);
    }
    mock().clear();
  }

  void given_manager_is_initialized()
  {
    mgr.init(
      &timer_group.timer_group,
      &adapter,
      &adapter_initialized,
      &bus);
  }

  void given_adapter_is_initialized()
  {
    esphome_mqtt_client_adapter_init(&adapter, "test_device");
    adapter_initialized = true;
    adapter_needs_destroy = true;
  }

  void given_mqtt_is_connected()
  {
    mock_mqtt.connected = true;
  }

  void given_mqtt_is_disconnected()
  {
    mock_mqtt.connected = false;
  }

  void after_one_tick()
  {
    tiny_timer_group_double_elapse_time(&timer_group, tick_period_ms);
  }

  void read_bus_state(uint8_t* out)
  {
    bus.read(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), out, sizeof(*out));
  }
};

// ── Initial state ─────────────────────────────────────────────────────────────

TEST(mqtt_connection_manager, initial_state_is_disconnected)
{
  given_manager_is_initialized();
  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::DISCONNECTED),
    static_cast<int>(mgr.get_state()));
}

// ── DISCONNECTED → SUBSCRIBING ───────────────────────────────────────────────

TEST(mqtt_connection_manager, transitions_to_subscribing_when_mqtt_connects)
{
  given_manager_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::SUBSCRIBING),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, fires_on_connected_event_when_mqtt_connects)
{
  given_manager_is_initialized();
  given_mqtt_is_connected();

  EventCounter connected_counter;
  connected_counter.attach(mgr.on_connected());

  after_one_tick();

  CHECK_EQUAL(1, connected_counter.count);
}

TEST(mqtt_connection_manager, writes_subscribing_state_to_bus_on_connect)
{
  given_manager_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();

  uint8_t state = 0xFF;
  bus.read(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &state, sizeof(state));
  CHECK_EQUAL(
    static_cast<uint8_t>(MqttConnectionState::SUBSCRIBING),
    state);
}

TEST(mqtt_connection_manager, does_not_fire_connected_again_on_second_tick)
{
  given_manager_is_initialized();
  given_mqtt_is_connected();

  EventCounter connected_counter;
  connected_counter.attach(mgr.on_connected());

  after_one_tick();
  after_one_tick();

  // Still in SUBSCRIBING (adapter not yet initialized), no second connect event.
  CHECK_EQUAL(1, connected_counter.count);
}

// ── SUBSCRIBING → FLUSHING ───────────────────────────────────────────────────

TEST(mqtt_connection_manager, stays_in_subscribing_while_adapter_not_initialized)
{
  given_manager_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // stays in SUBSCRIBING (adapter not ready)

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::SUBSCRIBING),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, transitions_to_flushing_when_adapter_ready)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING (adapter initialized, no pending updates)

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::FLUSHING),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, calls_subscribe_write_topic_when_adapter_ready)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING

  int subscribe_count_before = mock_mqtt.subscribe_call_count;
  after_one_tick();  // → FLUSHING (calls subscribe)

  CHECK_TRUE(mock_mqtt.subscribe_call_count > subscribe_count_before);
}

// ── FLUSHING → RUNNING ───────────────────────────────────────────────────────

TEST(mqtt_connection_manager, transitions_to_running_when_queue_is_empty)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING (queue empty)

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::RUNNING),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, transitions_to_running_without_adapter_init)
{
  // When adapter is NOT initialized, FLUSHING should advance to RUNNING anyway.
  given_manager_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING (adapter not initialized, stays here)

  // Force adapter_initialized so SUBSCRIBING can advance, then unset it.
  given_adapter_is_initialized();
  after_one_tick();  // → FLUSHING (adapter now ready, subscribed)

  // Unset adapter_initialized before FLUSHING tick.
  adapter_initialized = false;
  after_one_tick();  // → RUNNING (no adapter, go directly to RUNNING)

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::RUNNING),
    static_cast<int>(mgr.get_state()));
}

// ── RUNNING ───────────────────────────────────────────────────────────────────

TEST(mqtt_connection_manager, stays_in_running_while_mqtt_connected)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING
  after_one_tick();  // stays RUNNING
  after_one_tick();  // stays RUNNING

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::RUNNING),
    static_cast<int>(mgr.get_state()));
}

// ── Disconnect handling ───────────────────────────────────────────────────────

TEST(mqtt_connection_manager, stays_in_disconnected_when_mqtt_never_connects)
{
  given_manager_is_initialized();
  given_mqtt_is_disconnected();

  after_one_tick();
  after_one_tick();

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::DISCONNECTED),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, transitions_back_to_disconnected_from_running)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING

  given_mqtt_is_disconnected();
  after_one_tick();  // → DISCONNECTED

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::DISCONNECTED),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, fires_on_disconnected_event_when_mqtt_drops)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING

  EventCounter disconnected_counter;
  disconnected_counter.attach(mgr.on_disconnected());

  given_mqtt_is_disconnected();
  after_one_tick();  // → DISCONNECTED

  CHECK_EQUAL(1, disconnected_counter.count);
}

TEST(mqtt_connection_manager, on_disconnected_not_fired_while_already_disconnected)
{
  given_manager_is_initialized();
  given_mqtt_is_disconnected();

  EventCounter disconnected_counter;
  disconnected_counter.attach(mgr.on_disconnected());

  after_one_tick();
  after_one_tick();

  CHECK_EQUAL(0, disconnected_counter.count);
}

TEST(mqtt_connection_manager, writes_disconnected_state_to_bus_on_disconnect)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING

  given_mqtt_is_disconnected();
  after_one_tick();  // → DISCONNECTED

  uint8_t state = 0xFF;
  bus.read(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &state, sizeof(state));
  CHECK_EQUAL(static_cast<uint8_t>(MqttConnectionState::DISCONNECTED), state);
}

// ── Reconnect cycle ───────────────────────────────────────────────────────────

TEST(mqtt_connection_manager, can_reconnect_after_disconnect)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  after_one_tick();  // → SUBSCRIBING
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING

  given_mqtt_is_disconnected();
  after_one_tick();  // → DISCONNECTED

  given_mqtt_is_connected();
  after_one_tick();  // → SUBSCRIBING again

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::SUBSCRIBING),
    static_cast<int>(mgr.get_state()));
}

TEST(mqtt_connection_manager, fires_on_connected_on_each_reconnect)
{
  given_manager_is_initialized();
  given_adapter_is_initialized();
  given_mqtt_is_connected();

  EventCounter connected_counter;
  connected_counter.attach(mgr.on_connected());

  after_one_tick();  // → SUBSCRIBING (fires on_connected)
  after_one_tick();  // → FLUSHING
  after_one_tick();  // → RUNNING

  given_mqtt_is_disconnected();
  after_one_tick();  // → DISCONNECTED

  given_mqtt_is_connected();
  after_one_tick();  // → SUBSCRIBING (fires on_connected again)

  CHECK_EQUAL(2, connected_counter.count);
}

// ── Null global_mqtt_client ───────────────────────────────────────────────────

TEST(mqtt_connection_manager, does_nothing_when_global_mqtt_client_is_null)
{
  given_manager_is_initialized();
  esphome::mqtt::global_mqtt_client = nullptr;

  // Should not crash.
  after_one_tick();
  after_one_tick();

  CHECK_EQUAL(
    static_cast<int>(MqttConnectionState::DISCONNECTED),
    static_cast<int>(mgr.get_state()));

  // Restore for teardown.
  esphome::mqtt::global_mqtt_client = &mock_mqtt;
}
