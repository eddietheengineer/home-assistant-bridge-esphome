/*!
 * @file
 * @brief Timer-driven MQTT connection FSM.
 *
 * MqttConnectionManager owns the 4-state MQTT connection FSM that was
 * previously embedded in GeappliancesBridge::loop(). It registers a 100 ms
 * periodic timer in the shared timer_group so the FSM runs automatically
 * without any manual dispatch in loop().
 *
 * State diagram:
 *
 *   DISCONNECTED ──(connected)──▶ SUBSCRIBING ──(adapter_ready)──▶ FLUSHING ──(queue_empty)──▶ RUNNING
 *        ▲                                                                │                         │
 *        └────────────────────────────────────────────────────────────────┴──────(disconnect)───────┘
 *
 * On connect:   fires on_connected() — GeappliancesBridge forwards this to
 *               the startup HSM as signal_mqtt_connected.
 * On disconnect: calls esphome_mqtt_client_adapter_notify_disconnected() to
 *               reset the adapter state and publish on_mqtt_disconnect_event
 *               to the bridge modules; fires on_disconnected().
 * State changes: writes ERD_INTERNAL_MQTT_STATE to the ErdDataBus so that
 *               modules can react to connection state via ERD subscriptions
 *               rather than calling into this class directly.
 */

#pragma once

extern "C" {
#include "tiny_timer.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
}

#include "erd_data_bus.h"
#include "esphome_mqtt_client_adapter.h"

namespace esphome {
namespace geappliances_bridge {

enum class MqttConnectionState : uint8_t {
  DISCONNECTED = 0,
  SUBSCRIBING  = 1,
  FLUSHING     = 2,
  RUNNING      = 3,
};

class MqttConnectionManager {
 public:
  // Initialize the manager and start the periodic tick timer.
  //   timer_group      — shared timer group (must outlive this object)
  //   adapter          — MQTT client adapter (may not be initialized yet)
  //   adapter_initialized — pointer to bridge flag; checked before subscribing
  //   bus              — data bus for ERD_INTERNAL_MQTT_STATE writes
  void init(
    tiny_timer_group_t* timer_group,
    esphome_mqtt_client_adapter_t* adapter,
    const bool* adapter_initialized,
    ErdDataBus* bus);

  // Stop the tick timer.
  void destroy();

  MqttConnectionState get_state() const { return state_; }

  // Event fired when the FSM transitions DISCONNECTED → SUBSCRIBING.
  // Args: nullptr
  i_tiny_event_t* on_connected() { return &on_connected_.interface; }

  // Event fired when the FSM transitions any state → DISCONNECTED.
  // Args: nullptr
  i_tiny_event_t* on_disconnected() { return &on_disconnected_.interface; }

 private:
  static void tick_callback(void* context);
  void tick();

  tiny_timer_group_t* timer_group_{nullptr};
  esphome_mqtt_client_adapter_t* adapter_{nullptr};
  const bool* adapter_initialized_{nullptr};
  ErdDataBus* bus_{nullptr};
  tiny_timer_t timer_{};
  MqttConnectionState state_{MqttConnectionState::DISCONNECTED};
  tiny_event_t on_connected_{};
  tiny_event_t on_disconnected_{};
};

}  // namespace geappliances_bridge
}  // namespace esphome
