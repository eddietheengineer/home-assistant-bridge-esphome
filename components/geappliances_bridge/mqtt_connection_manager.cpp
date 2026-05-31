/*!
 * @file
 * @brief Timer-driven MQTT connection FSM implementation.
 */

#include "mqtt_connection_manager.h"
#include "erd_data_bus_ids.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char* TAG __attribute__((unused)) = "mqtt_conn_mgr";

void MqttConnectionManager::init(
  tiny_timer_group_t* timer_group,
  esphome_mqtt_client_adapter_t* adapter,
  const bool* adapter_initialized,
  ErdDataBus* bus)
{
  timer_group_ = timer_group;
  adapter_ = adapter;
  adapter_initialized_ = adapter_initialized;
  bus_ = bus;
  state_ = MqttConnectionState::DISCONNECTED;

  tiny_event_init(&on_connected_);
  tiny_event_init(&on_disconnected_);

  tiny_timer_start_periodic(timer_group_, &timer_, 1, this, tick_callback);
}

void MqttConnectionManager::destroy()
{
  if (timer_group_ != nullptr) {
    tiny_timer_stop(timer_group_, &timer_);
    timer_group_ = nullptr;
  }
}

void MqttConnectionManager::tick_callback(void* context)
{
  reinterpret_cast<MqttConnectionManager*>(context)->tick();
}

void MqttConnectionManager::tick()
{
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr) {
    return;
  }

  bool is_connected = mqtt_client->is_connected();

  if (!is_connected) {
    // Any state → DISCONNECTED on genuine loss of connection.
    if (state_ != MqttConnectionState::DISCONNECTED) {
      state_ = MqttConnectionState::DISCONNECTED;

      if (bus_ != nullptr) {
        auto s = static_cast<uint8_t>(state_);
        bus_->write(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &s, sizeof(s));
      }

      if (adapter_initialized_ != nullptr && *adapter_initialized_ &&
          adapter_ != nullptr) {
        esphome_mqtt_client_adapter_notify_disconnected(adapter_);
      }

      tiny_event_publish(&on_disconnected_, nullptr);
    }
    return;
  }

  // MQTT is connected — advance the FSM.
  switch (state_) {
    case MqttConnectionState::DISCONNECTED:
      ESP_LOGI(TAG, "MQTT connected");
      state_ = MqttConnectionState::SUBSCRIBING;

      if (bus_ != nullptr) {
        auto s = static_cast<uint8_t>(state_);
        bus_->write(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &s, sizeof(s));
      }

      tiny_event_publish(&on_connected_, nullptr);
      break;

    case MqttConnectionState::SUBSCRIBING:
      // Stay here until the adapter is ready (device_id generated).
      if (adapter_initialized_ != nullptr && *adapter_initialized_ &&
          adapter_ != nullptr) {
        esphome_mqtt_client_adapter_subscribe_write_topic(adapter_);
        state_ = MqttConnectionState::FLUSHING;

        if (bus_ != nullptr) {
          auto s = static_cast<uint8_t>(state_);
          bus_->write(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &s, sizeof(s));
        }
      }
      break;

    case MqttConnectionState::FLUSHING:
      // Drain pending ERD updates a few at a time; advance when queue is empty.
      if (adapter_initialized_ != nullptr && *adapter_initialized_ &&
          adapter_ != nullptr) {
        if (esphome_mqtt_client_adapter_drain_pending_updates(adapter_) == 0) {
          state_ = MqttConnectionState::RUNNING;

          if (bus_ != nullptr) {
            auto s = static_cast<uint8_t>(state_);
            bus_->write(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &s, sizeof(s));
          }
        }
      } else {
        state_ = MqttConnectionState::RUNNING;

        if (bus_ != nullptr) {
          auto s = static_cast<uint8_t>(state_);
          bus_->write(static_cast<tiny_erd_t>(ERD_INTERNAL_MQTT_STATE), &s, sizeof(s));
        }
      }
      break;

    case MqttConnectionState::RUNNING:
      // Steady-state: drain any newly queued ERD updates each tick.
      if (adapter_initialized_ != nullptr && *adapter_initialized_ &&
          adapter_ != nullptr) {
        esphome_mqtt_client_adapter_drain_pending_updates(adapter_);
      }
      break;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
