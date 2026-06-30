/*!
 * @file
 * @brief ERD cache MQTT publisher implementation.
 */

#include "erd_cache_mqtt_publisher.h"
#include "erd_cache.h"
#include "geappliances_bridge_log.h"
#include "i_mqtt_client.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/mqtt/mqtt_client.h"

#include <cstdio>
#include <string.h>

GEA_TAG(PUBLISHER_TAG) = "erd_cache_mqtt_publisher";
#ifdef USE_ESP_IDF
#include "esp_task_wdt.h"
#endif

#ifdef USE_ESP_IDF
static void mqtt_publisher_task(void* arg)
{
  erd_cache_mqtt_publisher_t* self = (erd_cache_mqtt_publisher_t*)arg;

  // Defensive: if semaphore creation failed, exit immediately.
  if (self->work_semaphore == NULL) {
    vTaskDelete(NULL);
    return;
  }

  while (self->task_running) {
    // Wait for work signal or timeout (100ms).
    if (xSemaphoreTake(self->work_semaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      // Work was signalled — drain all available updates.
    }

    // Acquire mutex to safely read shared state (mqtt_connected, cache pointers,
    // publish_index) and protect the entire drain loop. These fields can be
    // modified by the main loop during context switches.
    bool connected = false;
    bool has_deps = false;
    bool paused = false;
    bool mutex_held = false;
    if (self->state_mutex) {
      if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        mutex_held = true;
        connected = self->mqtt_connected;
        paused = self->paused;
        has_deps = self->cache != NULL && self->mqtt_client != NULL &&
                   self->device_id != NULL && self->get_time_ms != NULL;
      }
    } else {
      // Fallback when mutex creation failed — read without protection.
      connected = self->mqtt_connected;
      paused = self->paused;
      has_deps = self->cache != NULL && self->mqtt_client != NULL &&
                 self->device_id != NULL && self->get_time_ms != NULL;
    }

    if (!connected || paused || !has_deps) {
      if (mutex_held) {
        xSemaphoreGive(self->state_mutex);
      }
      continue;
    }

    // Drain all available updates — no per-loop budget in background task.
    // The mutex is held throughout to protect publish_index and cache access.
    bool drained_any = false;
    while (1) {
      erd_cache_entry_t* entry = erd_cache_get_next_updated(self->cache, &self->publish_index);
      if (!entry) break;
      drained_any = true;

      /* Determine data pointer. */
      const uint8_t* data;
      if (entry->uses_heap && entry->ext_data != NULL) {
        data = entry->ext_data;
      } else {
        data = entry->inline_data;
      }

      /* Build topic using pre-allocated buffer. */
      int topic_len = snprintf(self->task_topic, sizeof(self->task_topic),
          "geappliances/%s/erd/0x%04x/value", self->device_id, entry->erd);
      if (topic_len < 0 || (unsigned)topic_len >= sizeof(self->task_topic)) {
        ESP_LOGW(PUBLISHER_TAG, "MQTT topic truncated (device_id too long: %s)", self->device_id);
        break;
      }

      /* Build hex payload using pre-allocated buffer. */
      size_t data_len = entry->data_size;
      for (size_t i = 0; i < data_len; i++) {
        snprintf(self->task_hex + i * 2, 3, "%02x", data[i]);
      }
      self->task_hex[data_len * 2] = '\0';

      /* Publish through the interface. */
      uint32_t t_publish = self->get_time_ms();
      mqtt_client_publish_raw(self->mqtt_client, self->task_topic,
          self->task_hex, data_len * 2, true);
      uint32_t elapsed = self->get_time_ms() - t_publish;

      if (elapsed >= 1000) {
        ESP_LOGW(PUBLISHER_TAG, "Slow publish: %ums for ERD 0x%04x", elapsed, entry->erd);
      }

      /* Reload the publish cooldown after successful MQTT publish. */
      erd_cache_mark_published(self->cache, entry);

      // Update stats — already protected by the outer mutex hold.
      self->total_published++;
      self->publish_count_window++;
    }

    /* Detect full cache round: we drained entries and the index wrapped
     * back to 0, meaning we've scanned the entire cache. */
    if (drained_any && self->publish_index == 0) {
      self->first_round_done = true;
    }

    if (mutex_held) {
      xSemaphoreGive(self->state_mutex);
    }
  }

  // Signal completion before deleting the task (clean shutdown handshake).
  if (self->done_semaphore) {
    xSemaphoreGive(self->done_semaphore);
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
  self->mqtt_connected = false;
  self->get_time_ms = esphome::millis;

#ifdef USE_ESP_IDF
  self->work_semaphore = xSemaphoreCreateBinary();
  if (!self->work_semaphore) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create work semaphore");
  }
  self->state_mutex = xSemaphoreCreateMutex();
  if (!self->state_mutex) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create state mutex");
  }
  self->done_semaphore = xSemaphoreCreateBinary();
  if (!self->done_semaphore) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create done semaphore");
  }
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

  /* If MQTT is already connected when we register, set the flag so the
   * publisher knows it can publish.  Don't call on_connected() — that
   * logs "MQTT reconnected" which is misleading at init time. */
  auto global = esphome::mqtt::global_mqtt_client;
  if (global != nullptr && global->is_connected()) {
    self->mqtt_connected = true;
  }

  ESP_LOGI(PUBLISHER_TAG, "ERD cache MQTT publisher initialized with device ID: %s", self->device_id);
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
  if (self->state_mutex) {
    vSemaphoreDelete(self->state_mutex);
    self->state_mutex = NULL;
  }
  if (self->done_semaphore) {
    vSemaphoreDelete(self->done_semaphore);
    self->done_semaphore = NULL;
  }
#endif

  memset(self, 0, sizeof(*self));
}

void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->task_handle != NULL) return; // already running
  if (self->work_semaphore == NULL) return; // semaphore creation failed in init
  self->task_running = true;
  self->task_handle = xTaskCreateStatic(
      mqtt_publisher_task,
      "erd_mqtt_pub",
      2048 / sizeof(StackType_t),  /* words, matching task_stack[] size */
      self,
      2,
      self->task_stack,
      &self->task_tcb);
  if (self->task_handle == NULL) {
    ESP_LOGE(PUBLISHER_TAG, "Failed to create MQTT publisher task");
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
  if (self->work_semaphore != NULL) {
    xSemaphoreGive(self->work_semaphore);
  }
  // Wait for the task to signal completion via done_semaphore.
  // The task gives this semaphore before calling vTaskDelete, so we
  // know it has entered the termination path.
  if (self->done_semaphore != NULL) {
    if (xSemaphoreTake(self->done_semaphore, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGW(PUBLISHER_TAG, "MQTT publisher task did not signal done within 1 s");
    }
  } else {
    // Fallback: poll with delay when done_semaphore creation failed.
    uint32_t start = esphome::millis();
    while (self->task_handle != NULL && esphome::millis() - start < 1000) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (self->task_handle != NULL) {
      ESP_LOGW(PUBLISHER_TAG, "MQTT publisher task did not terminate within 1 s");
    }
  }

  // After vTaskDelete() the task is on xTasksWaitingTermination.
  // The idle task runs prvCheckTasksWaitingTermination to unlink the
  // TCB's list items via uxListRemove().  For StaticTask_t the TCB
  // memory is caller-owned so FreeRTOS doesn't free it, but it does
  // access the TCB's list pointers during unlinking.  We MUST yield
  // here so the idle task can finish before destroy() memsets the
  // struct — which would zero those list pointers mid-uxListRemove.
  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(100));
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
     * Defensive: if uses_heap is set but ext_data is NULL,
     * fall back to inline data to avoid a null dereference. */
    const uint8_t* data;
    if (entry->uses_heap && entry->ext_data != NULL) {
      data = entry->ext_data;
    } else {
      data = entry->inline_data;
    }

    /* Build topic: geappliances/{device_id}/erd/0x{ERD:04x}/value */
    char topic[128];
    int topic_len = snprintf(topic, sizeof(topic), "geappliances/%s/erd/0x%04x/value", self->device_id, entry->erd);
    if (topic_len < 0 || (unsigned)topic_len >= sizeof(topic)) {
      ESP_LOGW(PUBLISHER_TAG, "MQTT topic truncated (device_id too long: %s)", self->device_id);
      return published;
    }
    /* Build hex payload: max data_size is 255 (uint8_t), so hex is 510 chars + null */
    size_t data_len = entry->data_size;
    char hex[512];
    for (size_t i = 0; i < data_len; i++) {
      snprintf(hex + i * 2, 3, "%02x", data[i]);
    }
    hex[data_len * 2] = '\0';

    uint32_t t_publish = self->get_time_ms();
    mqtt_client_publish_raw(self->mqtt_client, topic, hex, data_len * 2, true);
    uint32_t elapsed = self->get_time_ms() - t_publish;

    if (elapsed >= 1000) {
      ESP_LOGW(PUBLISHER_TAG, "Slow publish: %ums for ERD 0x%04x", elapsed, entry->erd);
    }

    /* Reload the publish cooldown after successful MQTT publish. */
    erd_cache_mark_published(self->cache, entry);

    self->total_published++;
    self->publish_count_window++;
    published++;
  }
  /* Detect full cache round: drained entries and index wrapped to 0. */
  if (published > 0 && self->publish_index == 0) {
    self->first_round_done = true;
  }

  return published;
}

void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->mqtt_connected = true;
      self->first_round_done = false;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    self->mqtt_connected = true;
    self->first_round_done = false;
  }
#else
  self->mqtt_connected = true;
  self->first_round_done = false;
#endif
  ESP_LOGI(PUBLISHER_TAG, "MQTT reconnected — resuming ERD cache publishing");
  /* Wake the background task so it can start publishing again. */
  erd_cache_mqtt_publisher_signal_work(self);
}
void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->mqtt_connected = false;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    self->mqtt_connected = false;
  }
#else
  self->mqtt_connected = false;
#endif
  ESP_LOGW(PUBLISHER_TAG, "MQTT disconnected — pausing ERD cache publishing");
}

void erd_cache_mqtt_publisher_pause(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->paused = true;
      self->first_round_done = false;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    self->paused = true;
    self->first_round_done = false;
  }
#else
  self->paused = true;
  self->first_round_done = false;
#endif
}

void erd_cache_mqtt_publisher_resume(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      self->paused = false;
      xSemaphoreGive(self->state_mutex);
      // Wake the task so it can resume publishing.
      erd_cache_mqtt_publisher_signal_work(self);
    }
  } else {
    self->paused = false;
  }
#else
  self->paused = false;
#endif
}

void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void))
{
  self->get_time_ms = get_time_ms;
}

uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self)
{
  uint32_t count = 0;
#ifdef USE_ESP_IDF
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      count = self->publish_count_window;
      self->publish_count_window = 0;
      xSemaphoreGive(self->state_mutex);
    }
  } else {
    count = self->publish_count_window;
    self->publish_count_window = 0;
  }
#else
  count = self->publish_count_window;
  self->publish_count_window = 0;
#endif
  return count;
}

bool erd_cache_mqtt_publisher_first_round_done(erd_cache_mqtt_publisher_t* self)
{
#ifdef USE_ESP_IDF
  if (self->state_mutex) {
    if (xSemaphoreTake(self->state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      bool done = self->first_round_done;
      xSemaphoreGive(self->state_mutex);
      return done;
    }
  }
#endif
  return self->first_round_done;
}
