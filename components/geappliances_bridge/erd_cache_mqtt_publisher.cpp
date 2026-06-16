/*!
 * @file
 * @brief ERD cache MQTT publisher implementation.
 */

#include "erd_cache_mqtt_publisher.h"
#include "esphome_mqtt_client_adapter.h"
#include "esphome/core/log.h"

#include <stdio.h>
#include <string.h>

static const char* const TAG = "erd_cache_mqtt_publisher";

/* Build the MQTT topic: geappliances/{device_id}/erd/0x{ERD:04X}/value */
static void build_topic(char* buf, size_t buf_size, const char* device_id, uint16_t erd)
{
  snprintf(buf, buf_size, "geappliances/%s/erd/0x%04X/value", device_id, erd);
}

/* Build the hex payload: uppercase hex bytes, no separator, no prefix. */
static void build_hex_payload(char* buf, size_t max_out, const uint8_t* data, uint8_t data_size)
{
  if (data_size == 0 || max_out == 0) {
    if (max_out > 0) {
      buf[0] = '\0';
    }
    return;
  }
  char* p = buf;
  size_t out_len = 0;
  for (uint8_t i = 0; i < data_size; i++) {
    if (out_len + 2 >= max_out) {
      break;
    }
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", data[i]);
    *p++ = hex[0];
    *p++ = hex[1];
    out_len += 2;
  }
  *p = '\0';
}

void erd_cache_mqtt_publisher_init(
  erd_cache_mqtt_publisher_t* self,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client,
  const char* device_id)
{
  memset(self, 0, sizeof(*self));
  self->cache = cache;
  self->mqtt_client = mqtt_client;
  self->device_id = device_id;
  self->publish_index = 0;
  self->mqtt_connected = true;

  /* Subscribe to MQTT disconnect event */
  tiny_event_subscription_init(
    &self->mqtt_disconnect_subscription, self,
    +[](void* context, const void*) {
      erd_cache_mqtt_publisher_on_disconnected(
        reinterpret_cast<erd_cache_mqtt_publisher_t*>(context));
    });
  tiny_event_subscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);

  /* Subscribe to MQTT connect event */
  tiny_event_subscription_init(
    &self->mqtt_connect_subscription, self,
    +[](void* context, const void*) {
      erd_cache_mqtt_publisher_on_connected(
        reinterpret_cast<erd_cache_mqtt_publisher_t*>(context));
    });
  tiny_event_subscribe(
    mqtt_client_on_mqtt_connect(self->mqtt_client),
    &self->mqtt_connect_subscription);

  ESP_LOGI(TAG, "ERD cache MQTT publisher initialized");
}

void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self)
{
  if (!self->cache) {
    return;
  }

  if (self->mqtt_client) {
    tiny_event_unsubscribe(
      mqtt_client_on_mqtt_disconnect(self->mqtt_client),
      &self->mqtt_disconnect_subscription);
    tiny_event_unsubscribe(
      mqtt_client_on_mqtt_connect(self->mqtt_client),
      &self->mqtt_connect_subscription);
  }

  memset(self, 0, sizeof(*self));
}

uint16_t erd_cache_mqtt_publisher_loop(
  erd_cache_mqtt_publisher_t* self,
  uint16_t max_publishes,
  uint32_t max_ms)
{
  if (!self->cache || !self->mqtt_client || !self->device_id) {
    return 0;
  }

  if (!self->mqtt_connected) {
    self->dropped_count++;
    return 0;
  }

  uint32_t start_ms = esphome::millis();
  uint16_t published = 0;

  while (published < max_publishes) {
    erd_cache_entry_t* entry = erd_cache_get_next_updated(self->cache, &self->publish_index);
    if (!entry) {
      break;
    }

    if (esphome::millis() - start_ms >= max_ms) {
      break;
    }

    /* Determine data pointer */
    const uint8_t* data = entry->uses_heap ? entry->heap_data : entry->inline_data;

    /* Build topic and payload */
    char topic[64];
    build_topic(topic, sizeof(topic), self->device_id, entry->erd);

    char payload[128];
    build_hex_payload(payload, sizeof(payload), data, entry->data_size);

    /* Publish via the adapter's publish function.
     * The mqtt_client pointer is the adapter's interface sub-struct;
     * we recover the containing adapter struct to call the C++ publish. */
    esphome_mqtt_client_adapter_t* adapter = reinterpret_cast<esphome_mqtt_client_adapter_t*>(
      reinterpret_cast<char*>(self->mqtt_client) - offsetof(esphome_mqtt_client_adapter_t, interface));
    std::string topic_str(topic);
    std::string payload_str(payload);
    esphome_mqtt_client_adapter_publish(adapter, topic_str, payload_str, true);

    self->total_published++;
    published++;
  }

  return published;
}

void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self)
{
  self->mqtt_connected = true;
  ESP_LOGI(TAG, "MQTT reconnected — resuming ERD cache publishing");
}

void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self)
{
  self->mqtt_connected = false;
  ESP_LOGW(TAG, "MQTT disconnected — pausing ERD cache publishing");
}
