#include "esphome_mqtt_client_adapter.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

extern "C" {
#include "tiny_utils.h"
#include "tiny_event.h"
}

#include <cstdio>
#include <string>
#include <cctype>
#include <vector>

#ifdef USE_ESP_IDF
#include "esp_heap_caps.h"
#endif

static const char *const TAG __attribute__((unused)) = "geappliances_bridge.mqtt";


// ---------------------------------------------------------------------------
// publish_now: synchronous publish from the main ESPHome loop task.
// Must only be called from the main task — ESPHome's MQTT client is not
// designed for concurrent calls from other FreeRTOS tasks.
// ---------------------------------------------------------------------------

static void publish_now(const std::string& topic,
                        const std::string& payload,
                        bool retain)
{
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  if (mqtt_client != nullptr && mqtt_client->is_connected()) {
    mqtt_client->publish(topic, payload, 0, retain);
  }
}

static std::string build_topic(esphome_mqtt_client_adapter_t* self, const char* suffix)
{
  return std::string("geappliances/") + *self->device_id + suffix;
}

static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);

  // Track which ERDs the device registers so the bridge can filter
  // HA discovery entities to only those actually supported by the device.
  if (self->erd_registry != nullptr) {
    self->erd_registry->register_erd(erd);
  }

  ESP_LOGD(TAG, "Registered ERD 0x%04X", erd);
}

static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  // This function is no longer used — MqttSideStateMachine reads directly
  // from ErdStateTable and publishes via esphome_mqtt_client_adapter_publish().
  // Kept as stub for backward compatibility during Phase 4 transition.
  (void)_self; (void)erd; (void)value; (void)size;
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto self = reinterpret_cast<esphome_mqtt_client_adapter_t*>(_self);
  
  char topic_suffix[48];
  snprintf(topic_suffix, sizeof(topic_suffix), "/erd/0x%04x/write_result", erd);
  std::string topic = build_topic(self, topic_suffix);
  
  std::string payload = success ? "success" : "failure";
  if (!success) {
    char reason[16];
    snprintf(reason, sizeof(reason), " (reason: %d)", failure_reason);
    payload += reason;
  }
  
  publish_now(topic, payload, false);  // QoS 0, no retain
  
  ESP_LOGD(TAG, "Write result for ERD 0x%04X: %s", erd, payload.c_str());
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

static const i_mqtt_client_api_t api = {
  register_erd,
  update_erd,
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect
};

extern "C" void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id)
{
  self->interface.api = &api;
  self->device_id = new std::string(device_id);
  self->erd_registry = nullptr;
  self->wildcard_subscribed = false;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);
}

extern "C" bool esphome_mqtt_client_adapter_is_connected(
  const esphome_mqtt_client_adapter_t* self)
{
  (void)self;
  auto mqtt_client = esphome::mqtt::global_mqtt_client;
  return mqtt_client != nullptr && mqtt_client->is_connected();
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
  // Publish the disconnect event to notify the bridge HSMs.
  tiny_event_publish(&self->on_mqtt_disconnect_event, nullptr);
}

extern "C" void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self)
{
  // Subscribe once to a single wildcard topic that covers write commands for
  // ALL ERDs.  This replaces 100+ individual per-ERD subscribe() calls.
  if (!self->wildcard_subscribed) {
    auto mqtt_client = esphome::mqtt::global_mqtt_client;
    if (mqtt_client != nullptr && mqtt_client->is_connected()) {
      std::string wildcard_topic = build_topic(self, "/erd/+/write");
      ESP_LOGI(TAG, "Subscribing to wildcard write topic: %s", wildcard_topic.c_str());

      mqtt_client->subscribe(
        wildcard_topic,
        [self](const std::string& topic, const std::string& payload) {
          // Parse the ERD number from the topic.
          // Topic format: geappliances/{device_id}/erd/0xXXXX/write
          size_t write_pos = topic.rfind("/write");
          if (write_pos == std::string::npos || write_pos < 2) {
            ESP_LOGW(TAG, "Ignoring write message with unexpected topic: %s", topic.c_str());
            return;
          }
          size_t erd_start = topic.rfind('/', write_pos - 1);
          if (erd_start == std::string::npos) {
            ESP_LOGW(TAG, "Could not parse ERD from topic: %s", topic.c_str());
            return;
          }
          erd_start++;  // skip the '/'
          std::string erd_str = topic.substr(erd_start, write_pos - erd_start);
          char* end;
          unsigned long val = strtoul(erd_str.c_str(), &end, 16);
          if (*end != '\0' || val > 0xFFFF) {
            ESP_LOGW(TAG, "Invalid ERD value in topic: %s", topic.c_str());
            return;
          }
          tiny_erd_t erd = static_cast<tiny_erd_t>(val);

          ESP_LOGD(TAG, "Write request for ERD 0x%04X: %s", erd, payload.c_str());

          if (payload.length() % 2 != 0) {
            ESP_LOGW(TAG, "Invalid hex payload for ERD 0x%04X: odd length (%zu)", erd, payload.length());
            return;
          }

          std::vector<uint8_t> data;
          data.reserve(payload.length() / 2);
          for (size_t i = 0; i < payload.length(); i += 2) {
            char byte_str[3] = {payload[i], payload[i + 1], '\0'};
            if (!std::isxdigit(static_cast<unsigned char>(payload[i])) ||
                !std::isxdigit(static_cast<unsigned char>(payload[i + 1]))) {
              ESP_LOGW(TAG, "Invalid hex characters in payload for ERD 0x%04X at position %zu", erd, i);
              return;
            }
            data.push_back(static_cast<uint8_t>(strtol(byte_str, nullptr, 16)));
          }

          if (data.empty() || data.size() > 255) {
            ESP_LOGW(TAG, "Invalid data size for ERD 0x%04X: %zu bytes", erd, data.size());
            return;
          }

          mqtt_client_on_write_request_args_t args = {
            .erd  = erd,
            .size = static_cast<uint8_t>(data.size()),
            .value = data.data()
          };
          tiny_event_publish(&self->on_write_request_event, &args);
        },
        0  // QoS 0
      );
      self->wildcard_subscribed = true;
    }
  }
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
  esphome_mqtt_client_adapter_t* /*self*/,
  const std::string& topic,
  const std::string& payload,
  bool retain)
{
  publish_now(topic, payload, retain);
}
