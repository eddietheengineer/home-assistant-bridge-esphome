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



static const char* write_failure_reason_to_string(tiny_gea3_erd_client_write_failure_reason_t reason)
{
  switch (reason) {
    case tiny_gea3_erd_client_write_failure_reason_retries_exhausted: return "retries_exhausted";
    case tiny_gea3_erd_client_write_failure_reason_not_supported: return "not_supported";
    case tiny_gea3_erd_client_write_failure_reason_incorrect_size: return "incorrect_size";
    default: return "unknown";
  }
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) return;

  char topic[128];
  snprintf(topic, sizeof(topic), "geappliances/%s/erd/0x%04x/write_result",
           self->device_id->c_str(), erd);

  std::string payload;
  if (success) {
    payload = "ok";
  } else {
    payload = "{\"error\":\"" + std::string(write_failure_reason_to_string(failure_reason)) + "\"}";
  }

  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload.c_str());
  mqtt_client->publish(topic, payload, 0, true);
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
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect,
  on_mqtt_connect,
  esphome_mqtt_client_adapter_publish_raw
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->interface.api = &api;
  self->device_id = new std::string(device_id);
  self->erd_registry = nullptr;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
  tiny_event_init(&self->on_mqtt_connect_event);

  // Wire ESPHome MQTT client connect/disconnect callbacks to our tiny events.
  // Without this, mqtt_connected stays false forever and the publisher never publishes.
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr) {
    mqtt_client->set_on_connect([self](bool) {
      esphome_mqtt_client_adapter_notify_connected(self);
    });
    mqtt_client->set_on_disconnect([self](esphome::mqtt::MQTTClientDisconnectReason) {
      esphome_mqtt_client_adapter_notify_disconnected(self);
    });

    // If already connected when we register, fire the event immediately so the
    // publisher's mqtt_connected flag is set correctly on first loop().
    if (mqtt_client->is_connected()) {
      esphome_mqtt_client_adapter_notify_connected(self);
    }
  }
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
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client == nullptr) return;

  char topic[128];
  snprintf(topic, sizeof(topic), "geappliances/%s/erd/+/write",
           self->device_id->c_str());

  mqtt_client->subscribe(topic, [self](const std::string& topic, const std::string& payload) {
    // Parse ERD from topic: geappliances/{device_id}/erd/0x{ERD}/write
    auto pos = topic.find("/erd/0x");
    if (pos == std::string::npos) return;

    const char* erd_str = topic.c_str() + pos + 5; // skip "erd/"

    unsigned erd = 0;
    sscanf(erd_str, "%x", &erd);

    mqtt_client_on_write_request_args_t args;
    args.erd = static_cast<tiny_erd_t>(erd);
    args.size = payload.size();
    args.value = reinterpret_cast<const void*>(payload.c_str());

    tiny_event_publish(&self->on_write_request_event, &args);
  }, 0);
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
  (void)self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, payload, 0, retain);
  }
}
extern "C" void esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* _self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain)
{
  (void)_self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, std::string(payload, payload_len), 0, retain);
  }
}

extern "C" size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  return 0;
}
