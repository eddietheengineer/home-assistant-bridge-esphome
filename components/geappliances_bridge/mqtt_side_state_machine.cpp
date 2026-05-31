// =============================================================================
// MqttSideStateMachine implementation
// =============================================================================
// Extracted from the inline MqttConnectionState FSM in GeappliancesBridge.
// Reads flagged ERDs from ErdStateTable and publishes them to MQTT.
// =============================================================================

#include "mqtt_side_state_machine.h"

#include <string>

namespace esphome {
namespace geappliances_bridge {

MqttSideStateMachine::MqttSideStateMachine(
    ErdStateTable* state_table,
    GlobalStateRegistry* registry,
    ErdRegistry* erd_registry,
    esphome_mqtt_client_adapter_t* adapter,
    WriteRouter* write_router)
    : state_table_(state_table),
      registry_(registry),
      erd_registry_(erd_registry),
      adapter_(adapter),
      write_router_(write_router),
      state_(State::DISCONNECTED) {
}

void MqttSideStateMachine::loop() {
  switch (state_) {
    case State::DISCONNECTED:
      // No-op — publish_flags accumulate in ErdStateTable while disconnected.
      break;

    case State::SUBSCRIBING:
      esphome_mqtt_client_adapter_subscribe_write_topic(adapter_);
      state_ = State::FLUSHING;
      break;

    case State::FLUSHING:
      drain_flagged_erds_();
      if (state_table_->get_flagged_erds().empty()) {
        state_ = State::RUNNING;
      }
      break;

    case State::RUNNING:
      drain_flagged_erds_();
      break;
  }
}

MqttSideStateMachine::State MqttSideStateMachine::get_current_state() const {
  return state_;
}

void MqttSideStateMachine::on_mqtt_connected() {
  switch (state_) {
    case State::DISCONNECTED:
      state_ = State::SUBSCRIBING;
      break;

    case State::SUBSCRIBING:
    case State::FLUSHING:
    case State::RUNNING:
      // Already connected or in a connected state — go to flushing to
      // re-drain any flags that accumulated during a brief disconnect.
      state_ = State::FLUSHING;
      break;
  }
}

void MqttSideStateMachine::on_mqtt_disconnected() {
  state_ = State::DISCONNECTED;
}

void MqttSideStateMachine::drain_flagged_erds_() {
  size_t flushed = 0;
  for (auto erd_id : state_table_->get_flagged_erds()) {
    if (!esphome_mqtt_client_adapter_is_connected(adapter_) ||
        flushed >= MAX_FLUSH_PER_CALL) {
      break;
    }
    uint8_t size = 0;
    const uint8_t* value = state_table_->get_erd_value(erd_id, size);
    if (value == nullptr) {
      continue;
    }
    std::string topic = build_erd_topic(registry_->get_device_id(), erd_id);
    std::string payload = format_erd_payload(erd_id, value, size, erd_registry_);
    esphome_mqtt_client_adapter_publish(adapter_, topic, payload, true);
    state_table_->clear_publish_flag(erd_id);
    flushed++;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
