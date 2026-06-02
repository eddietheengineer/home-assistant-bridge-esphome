// =============================================================================
// MqttSideStateMachine implementation
// =============================================================================
// Extracted from the inline MqttConnectionState FSM in GeappliancesBridge.
// Reads flagged ERDs from ErdStateTable and publishes them to MQTT.
// =============================================================================

#include "mqtt_side_state_machine.h"

#include <string>
#include "esphome/core/log.h"

static const char* const TAG = "mqtt_fsm";

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
  // If MQTT is already connected when we're constructed, transition immediately.
  // This handles the common case where MQTT connects during the device_id phase
  // before the FSM is created in initialize_mqtt_client_().
  if (esphome_mqtt_client_adapter_is_connected(adapter)) {
    state_ = State::SUBSCRIBING;
    ESP_LOGD(TAG, "Constructor: MQTT already connected, starting in SUBSCRIBING");
  }
}

void MqttSideStateMachine::loop() {
  switch (state_) {
    case State::DISCONNECTED:
      // No-op — publish_flags accumulate in ErdStateTable while disconnected.
      break;

    case State::SUBSCRIBING:
      ESP_LOGD(TAG, "SUBSCRIBING: subscribing to write topic");
      esphome_mqtt_client_adapter_subscribe_write_topic(adapter_);
      state_ = State::FLUSHING;
      ESP_LOGD(TAG, "FLUSHING: flagged ERDs: %zu", state_table_->get_flagged_erds().size());
      break;

    case State::FLUSHING:
      drain_flagged_erds_();
      if (state_table_->get_flagged_erds().empty()) {
        state_ = State::RUNNING;
        ESP_LOGD(TAG, "RUNNING: all flagged ERDs flushed");
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
      ESP_LOGD(TAG, "on_mqtt_connected: DISCONNECTED -> SUBSCRIBING");
      state_ = State::SUBSCRIBING;
      break;

    case State::SUBSCRIBING:
    case State::FLUSHING:
    case State::RUNNING:
      // Already connected or in a connected state — go to flushing to
      // re-drain any flags that accumulated during a brief disconnect.
      ESP_LOGD(TAG, "on_mqtt_connected: %s -> FLUSHING", 
               state_ == State::SUBSCRIBING ? "SUBSCRIBING" :
               state_ == State::FLUSHING ? "FLUSHING" : "RUNNING");
      state_ = State::FLUSHING;
      break;
  }
}

void MqttSideStateMachine::on_mqtt_disconnected() {
  state_ = State::DISCONNECTED;
}

void MqttSideStateMachine::drain_flagged_erds_() {
  std::vector<tiny_erd_t> flagged = state_table_->get_flagged_erds();
  if (flagged.empty()) {
    return;
  }
  size_t flushed = 0;
  for (auto erd_id : flagged) {
    if (flushed >= MAX_FLUSH_PER_CALL) {
      break;
    }
    // Check connection before each publish — if MQTT disconnects mid-loop,
    // the remaining flagged ERDs stay flagged for the next reconnect.
    if (!esphome_mqtt_client_adapter_is_connected(adapter_)) {
      break;
    }
    uint8_t size = 0;
    const uint8_t* value = state_table_->get_erd_value(erd_id, size);
    if (value == nullptr) {
      state_table_->clear_publish_flag(erd_id);
      continue;
    }
    std::string topic = build_erd_topic(registry_->get_device_id(), erd_id);
    std::string payload = format_erd_payload(erd_id, value, size, erd_registry_);
    esphome_mqtt_client_adapter_publish(adapter_, topic, payload, true);
    state_table_->clear_publish_flag(erd_id);
    // Debug log: ERD number and raw data bytes in hex
    char data_hex[65];  // 32 bytes * 2 hex chars + null
    data_hex[0] = '\0';
    for (uint8_t i = 0; i < size && i < 32; i++) {
      sprintf(data_hex + (i * 2), "%02X", value[i]);
    }
    ESP_LOGD(TAG, "drain: published 0x%04X Data: %s", erd_id, data_hex);
    flushed++;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
