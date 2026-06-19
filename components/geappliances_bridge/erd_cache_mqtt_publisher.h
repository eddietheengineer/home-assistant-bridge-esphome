/*!
 * @file
 * @brief Scans the shared ERD cache and publishes updated ERDs
 *        to MQTT topics with retain=true.
 *
 * On ESP-IDF platforms, publishing runs in a FreeRTOS background task
 * to avoid blocking the ESPHome main loop on the IDF MQTT mutex.
 * On non-ESP-IDF platforms, erd_cache_mqtt_publisher_loop() is called
 * directly from the main loop as before.
 *
 * Responsibilities:
 *   - Iterate cache entries with update_required=true
 *   - Publish to geappliances/{deviceId}/erd/0x{ERD:04X}/value
 *   - Pause on MQTT disconnect, resume on reconnect
 *
 * NOT responsible for:
 *   - Cache lifecycle (owned by GeappliancesBridge)
 *   - MQTT connection lifecycle (owned by EsphomeMqttClientAdapter)
 *   - Write commands (out of scope)
 */

#ifndef erd_cache_mqtt_publisher_h
#define erd_cache_mqtt_publisher_h

#include <stdint.h>
#include <stdbool.h>

#include "erd_cache.h"
#include "i_mqtt_client.h"
#include "i_tiny_event.h"

#ifdef USE_ESP_IDF
#  ifdef USE_ESP_IDF_STUBS
#    include "esp-idf/freertos_stub.h"
#  else
#    include "freertos/FreeRTOS.h"
#    include "freertos/task.h"
#    include "freertos/semphr.h"
#  endif
#endif

typedef struct {
  erd_cache_t* cache;              // Shared cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;    // MQTT publish interface
  const char* device_id;           // Device ID string for topic construction
  uint16_t publish_index;          // Round-robin index into cache entries
  bool mqtt_connected;             // True when MQTT broker is connected
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t mqtt_connect_subscription;
  // Stats
  uint32_t total_published;        // Total ERD publishes since init
  uint32_t missed_loops;           // Loop iterations skipped while MQTT disconnected
  uint32_t publish_count_window;   // Publishes in the last 60s window
  uint32_t (*get_time_ms)(void);

#ifdef USE_ESP_IDF
  TaskHandle_t    task_handle;
  StaticTask_t    task_tcb;
  StackType_t     task_stack[1024 / sizeof(StackType_t)];
  SemaphoreHandle_t work_semaphore;
  bool task_running;
#endif
} erd_cache_mqtt_publisher_t;

#ifdef __cplusplus
extern "C" {
#endif

void erd_cache_mqtt_publisher_init(
  erd_cache_mqtt_publisher_t* self,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client,
  const char* device_id);

void erd_cache_mqtt_publisher_destroy(erd_cache_mqtt_publisher_t* self);

/*!
 * Start the background publishing task (ESP-IDF only; no-op otherwise).
 * Call after init() to begin draining the cache in a background task.
 */
void erd_cache_mqtt_publisher_start(erd_cache_mqtt_publisher_t* self);

/*!
 * Stop the background publishing task (ESP-IDF only; no-op otherwise).
 * Call from destroy() or teardown to cleanly shut down the task.
 */
void erd_cache_mqtt_publisher_stop(erd_cache_mqtt_publisher_t* self);

/*!
 * Signal the background task that there is work to do (ESP-IDF only; no-op otherwise).
 * Call from the main loop when cache entries have been updated.
 */
void erd_cache_mqtt_publisher_signal_work(erd_cache_mqtt_publisher_t* self);

/*!
 * Returns the number of ERDs actually published.
 * No-ops if MQTT is disconnected (increments missed_loops).
 * On ESP-IDF, this is called from the background task, not the main loop.
 */
uint16_t erd_cache_mqtt_publisher_loop(
  erd_cache_mqtt_publisher_t* self,
  uint16_t max_publishes,
  uint32_t max_ms);

/*!
 * Called when MQTT broker connects.
 */
void erd_cache_mqtt_publisher_on_connected(erd_cache_mqtt_publisher_t* self);

/*!
 * Called when MQTT broker disconnects.
 */
void erd_cache_mqtt_publisher_on_disconnected(erd_cache_mqtt_publisher_t* self);

/*!
 * Override the time source (defaults to esphome::millis).
 * Useful for testing.
 */
void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void));

/*!
 * Returns the number of ERD publishes in the last 60 seconds, then resets the window.
 */
uint32_t erd_cache_mqtt_publisher_get_publish_rate(erd_cache_mqtt_publisher_t* self);

#ifdef __cplusplus
}
#endif

#endif
