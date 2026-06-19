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

#ifdef USE_ESP_IDF
static void mqtt_publisher_task(void* arg)
{
  erd_cache_mqtt_publisher_t* self = (erd_cache_mqtt_publisher_t*)arg;

  while (self->task_running) {
    // Wait for work signal or timeout (100ms).
    if (xSemaphoreTake(self->work_semaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      // Work was signalled — drain all available updates.
    }

    // Skip if not connected.
    if (!self->mqtt_connected || !self->cache || !self->mqtt_client ||
        !self->device_id || !self->get_time_ms) {
      continue;
    }

    // Drain all available updates — no per-loop budget in background task.
    while (1) {
      erd_cache_entry_t* entry = erd_cache_get_next_updated(self->cache, &self->publish_index);
      if (!entry) break;

      /* Determine data pointer. */
      const uint8_t* data;
      if ((entry->uses_heap || entry->uses_pool) && entry->ext_data != NULL) {
        data = entry->ext_data;
      } else {
        data = entry->inline_data;
      }

      /* Build topic: geappliances/{device_id}/erd/0x{ERD:04x}/value */
      char topic[128];
      int topic_len = snprintf(topic, sizeof(topic),
          "geappliances/%s/erd/0x%04x/value", self->device_id, entry->erd);
      if (topic_len < 0 || (unsigned)topic_len >= sizeof(topic)) {
        ESP_LOGW(TAG, "MQTT topic truncated (device_id too long: %s)", self->device_id);
        break;
      }

      /* Build hex payload. */
      size_t data_len = entry->data_size;
      char hex[512];
      for (size_t i = 0; i < data_len; i++) {
        snprintf(hex + i * 2, 3, "%02x", data[i]);
      }
      hex[data_len * 2] = '\0';

      /* Publish through the interface. */
      uint32_t t_publish = self->get_time_ms();
      mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true);
      uint32_t elapsed = self->get_time_ms() - t_publish;

      if (elapsed >= 50) {
        ESP_LOGW(TAG, "Slow publish: %ums for ERD 0x%04x", elapsed, entry->erd);
      }

      self->total_published++;
      self->publish_count_window++;
    }
  }

  vTaskDelete(NULL);
}
#endif

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
  self->get_time_ms = esphome::millis;

#ifdef USE_ESP_IDF
  self->work_semaphore = xSemaphoreCreateBinary();
  self->task_running = false;
#endif

  if (!mqtt_client) return;
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
  erd_cache_mqtt_publisher_stop(self);

  if (!self->mqtt_client) {
    memset(self, 0, sizeof(*self));
    return;
  }

  tiny_event_unsubscribe(
    mqtt_client_on_mqtt_disconnect(self->mqtt_client),
    &self->mqtt_disconnect_subscription);
  tiny_event_unsubscribe(
    mqtt_client_on_mqtt_connect(self->mqtt_client),
    &self->mqtt_connect_subscription);

#ifdef USE_ESP_IDF
  if (self->work_semaphore) {
    vSemaphoreDelete(self->work_semaphore);
    self->work_semaphore = NULL;
  }
#endif

  memset(self, 0, sizeof(*self));
}

void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->task_handle != NULL) return; // already running
  self->task_running = true;
  self->task_handle = xTaskCreateStatic(
      mqtt_publisher_task,
      "erd_mqtt_pub",
      1024,
      self,
      2,
      self->task_stack,
      &self->task_tcb);
  if (self->task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create MQTT publisher task");
    self->task_running = false;
  }
#else
  (void)self;
#endif
}

void erd_cache_mqtt_publisher_stop(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->task_handle == NULL) return;
  self->task_running = false;
  // Wake the task so it can exit.
  xSemaphoreGive(self->work_semaphore);
  // Wait for task to finish (up to 1 second).
  UBaseType_t ticks = pdMS_TO_TICKS(1000);
  while (self->task_handle != NULL && ticks-- > 0) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  self->task_handle = NULL;
#else
  (void)self;
#endif
}

void erd_cache_mqtt_publisher_signal_work(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->work_semaphore != NULL) {
    // Non-blocking give — if task is already waiting, it will wake up.
    xSemaphoreGive(self->work_semaphore);
  }
#else
  (void)self;
#endif
}

uint16_t erd_cache_mqtt_publisher_loop(
  erd_cache_mqtt_publisher_t* self,
  uint16_t max_publishes,
  uint32_t max_ms)
{
  if (!self->cache || !self->mqtt_client || !self->device_id || !self->get_time_ms) {
    return 0;
  }

  if (!self->mqtt_connected) {
    self->missed_loops++;
    return 0;
  }
  uint32_t start_ms = self->get_time_ms();
  uint16_t published = 0;

  while (published < max_publishes) {
    erd_cache_entry_t* entry = erd_cache_get_next_updated(self->cache, &self->publish_index);
    if (!entry) {
      break;
    }

    if (self->get_time_ms() - start_ms >= max_ms) {
      break;
    }
    /* Determine data pointer.
     * Defensive: if uses_pool or uses_heap is set but ext_data is NULL,
     * fall back to inline data to avoid a null dereference. */
    const uint8_t* data;
    if ((entry->uses_heap || entry->uses_pool) && entry->ext_data != NULL) {
      data = entry->ext_data;
    } else {
      data = entry->inline_data;
    }

    /* Build topic: geappliances/{device_id}/erd/0x{ERD:04x}/value */
    char topic[128];
    int topic_len = snprintf(topic, sizeof(topic), "geappliances/%s/erd/0x%04x/value", self->device_id, entry->erd);
    if (topic_len < 0 || (unsigned)topic_len >= sizeof(topic)) {
      ESP_LOGW(TAG, "MQTT topic truncated (device_id too long: %s)", self->device_id);
      return published;
    }
    /* Build hex payload: max data_size is 255 (uint8_t), so hex is 510 chars + null */
    size_t data_len = entry->data_size;
    char hex[512];
    for (size_t i = 0; i < data_len; i++) {
      snprintf(hex + i * 2, 3, "%02x", data[i]);
    }
    hex[data_len * 2] = '\0';

    /* Publish through the interface — measure per-publish time. */
    uint32_t t_publish = self->get_time_ms();
    mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true);
    uint32_t elapsed = self->get_time_ms() - t_publish;

    if (elapsed >= 50) {
      ESP_LOGW(TAG, "Slow publish: %ums for ERD 0x%04x", elapsed, entry->erd);
    }

    self->total_published++;
    self->publish_count_window++;
    published++;
  }

  return published;
}

void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self)
{
  self->mqtt_connected = true;
  ESP_LOGI(TAG, "MQTT reconnected — resuming ERD cache publishing");
  /* Wake the background task so it can start publishing again. */
  erd_cache_mqtt_publisher_signal_work(self);
}

void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self)
{
  self->mqtt_connected = false;
  ESP_LOGW(TAG, "MQTT disconnected — pausing ERD cache publishing");
}

void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void))
{
  self->get_time_ms = get_time_ms;
}

uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self)
{
  uint32_t count = self->publish_count_window;
  self->publish_count_window = 0;
  return count;
}
