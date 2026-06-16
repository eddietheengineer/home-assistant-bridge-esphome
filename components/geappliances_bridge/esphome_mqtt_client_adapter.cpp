#include "esphome_mqtt_client_adapter.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

extern "C" {
#include "tiny_utils.h"
#include "tiny_event.h"
}

#include <cstdio>
#include <string>

static const char *const TAG __attribute__((unused)) = "geappliances_bridge.mqtt";


static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  if (self->erd_registry != nullptr) {
    self->erd_registry->register_erd(erd);
  }

  ESP_LOGD(TAG, "Registered ERD 0x%04X", erd);
}

static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  if (self->erd_registry != nullptr && !self->erd_registry->is_valid(erd)) {
    return;
  }

  if (value == nullptr || size == 0) {
    ESP_LOGW(TAG, "Invalid ERD update: null value or zero size for ERD 0x%04X", erd);
    return;
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
  std::string hex;
  hex.reserve(size * 2);
  for (uint8_t i = 0; i < size; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", bytes[i]);
    hex += buf;
  }


  ESP_LOGV(TAG, "ERD 0x%04X: %s", erd, hex.c_str());
  self->erd_publish_count_++;
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t /*failure_reason*/)
{
  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, success ? "success" : "failure");
  (void)_self; (void)erd; (void)success;
}

static i_tiny_event_t* on_write_request(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  return &self->on_write_request_event.interface;
}

static i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  return &self->on_mqtt_disconnect_event.interface;
}

static i_tiny_event_t* on_mqtt_connect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  return &self->on_mqtt_connect_event.interface;
}

static const i_mqtt_client_api_t api = {
  register_erd,
  update_erd,
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect,
  on_mqtt_connect
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->interface.api = &api;
  self->device_id = new std::string(device_id);
  self->erd_registry = nullptr;
  self->erd_publish_count_ = 0;
  self->mqtt_publish_count_ = 0;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
  tiny_event_init(&self->on_mqtt_connect_event);
}

extern "C" void esphome_mqtt_client_adapter_set_erd_registry(
  esphome_mqtt_client_adapter_t* self,
  esphome::geappliances_bridge::ErdRegistry* erd_registry)
{
  self->erd_registry = erd_registry;
}
extern "C" void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self)
{
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  // No MQTT broker — write command subscriptions are not available.
}

extern "C" size_t esphome_mqtt_client_adapter_drain_pending_updates(
  esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  return 0;
}
extern "C" void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self)
{
  tiny_event_publish(&self->on_mqtt_connect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self)
{
  if (self->device_id != nullptr) {
    delete self->device_id;
    self->device_id = nullptr;
  }
}

extern "C" void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* self,
  const std::string& topic,
  const std::string& payload,
  bool retain)
{
  self->mqtt_publish_count_++;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, payload, 0, retain);
  }
}

extern "C" size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  return 0;
}
extern "C" uint32_t esphome_mqtt_client_adapter_get_and_reset_erd_publish_count(
  esphome_mqtt_client_adapter_t* self)
{
  uint32_t count = self->erd_publish_count_;
  self->erd_publish_count_ = 0;
  return count;
}

extern "C" uint32_t esphome_mqtt_client_adapter_get_and_reset_mqtt_publish_count(
  esphome_mqtt_client_adapter_t* self)
{
  uint32_t count = self->mqtt_publish_count_;
  self->mqtt_publish_count_ = 0;
  return count;
}
