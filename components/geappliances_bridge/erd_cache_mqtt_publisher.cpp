/*!
 * @file
 * @brief ERD cache MQTT publisher implementation.
 */

#include "erd_cache_mqtt_publisher.h"
#include "i_mqtt_client.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <cstdio>
#include <string.h>

static const char* const TAG = "erd_cache_mqtt_publisher";


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

    /* Build topic: geappliances/{device_id}/erd/0x{ERD:04X}/value */
    char topic[128];
    snprintf(topic, sizeof(topic), "geappliances/%s/erd/0x%04X/value", self->device_id, entry->erd);

    /* Build hex payload: max data_size is 255 (uint8_t), so hex is 510 chars + null */
    size_t data_len = entry->data_size;
    char hex[512];
    for (size_t i = 0; i < data_len; i++) {
      snprintf(hex + i * 2, 3, "%02X", data[i]);
    }
    hex[data_len * 2] = '\0';

    /* Publish through the interface */
    mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true);

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
