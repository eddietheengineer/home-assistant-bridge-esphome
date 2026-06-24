/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Streaming design: decompresses embedded JSONL entity definitions one chunk
 * at a time, publishes each entity's discovery payload to MQTT immediately,
 * then discards the chunk before moving to the next.  No large queue is
 * needed — peak memory is the decompress buffer plus one topic/payload pair.
 *
 * Single-task design:
 *   One background FreeRTOS task walks categories → chunks → lines,
 *   publishing each entity inline.  It yields (vTaskDelay 0) after every
 *   chunk so the main loop and other tasks are never starved.
 *
 * States: IDLE -> PROCESSING -> COMPLETE / FAILED
 *
 * All FreeRTOS task code is guarded with #ifdef USE_ESP_IDF for
 * simulator/test build compatibility.
 */

#ifndef ha_discovery_manager_h
#define ha_discovery_manager_h

#include <stdint.h>
#include <stdbool.h>

#include "erd_cache.h"
#include "i_mqtt_client.h"

#ifdef USE_ESP_IDF
#  ifdef USE_ESP_IDF_STUBS
#    include "esp-idf/freertos_stub.h"
#  else
#    include "freertos/FreeRTOS.h"
#    include "freertos/task.h"
#    include "freertos/semphr.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Discovery manager states */
typedef enum {
  ha_discovery_state_idle,
  ha_discovery_state_processing,
  ha_discovery_state_complete,
  ha_discovery_state_failed
} ha_discovery_state_t;

/*!
 * @brief Home Assistant MQTT Discovery manager.
 *
 * All buffers are pre-allocated — no heap allocation during processing.
 * Peak memory: decompress buffer (~16 KB) + one topic/payload pair (~640 B).
 */
typedef struct {
  erd_cache_t* cache;              // Shared ERD cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;    // MQTT publish interface
  const char* device_id;           // Device ID string for topic construction
  const char* model_number;        // Model number for device info
  const char* serial_number;       // Serial number for device info

  ha_discovery_state_t state;

  /* Stats */
  uint32_t total_discovered;       // Total entities discovered
  uint32_t total_published;        // Total discovery publishes
  uint32_t total_filtered;         // Entities filtered out (ERD not registered)

  /* Time source */
  uint32_t (*get_time_ms)(void);

#ifdef USE_ESP_IDF
  TaskHandle_t    task_handle;
  StaticTask_t    task_tcb;
  StackType_t     task_stack[2048 / sizeof(StackType_t)];

  SemaphoreHandle_t done_semaphore;
  bool task_running;

  /* Pre-allocated buffers for the background task. */
  char topic_buf[128];
  char payload_buf[512];
  char line_buf[512];
  /* Buffer for chunked decompression. Sized for the largest single JSONL
   * line (~14KB for range.jsonl select entities with many options). */
  uint8_t decompress_buf[16384];
#endif
} ha_discovery_manager_t;

/*!
 * Initialize the discovery manager.
 * Call once before configure().
 */
void ha_discovery_manager_init(ha_discovery_manager_t* self);

/*!
 * Configure the discovery manager with device info and dependencies.
 * Call after init(), before start().
 */
void ha_discovery_manager_configure(
  ha_discovery_manager_t* self,
  const char* device_id,
  const char* model_number,
  const char* serial_number,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client);

/*!
 * Start the discovery process.
 * On ESP-IDF, spawns a background task that streams: decompress → publish → discard.
 * On non-ESP-IDF, no-op (marks complete).
 */
void ha_discovery_manager_start(ha_discovery_manager_t* self);

/*!
 * Clean up the discovery manager.
 * Stops tasks and frees resources. Call from teardown.
 */
void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);

/*!
 * Returns true if the manager is currently processing (decompressing/publishing).
 */
bool ha_discovery_manager_is_processing(ha_discovery_manager_t* self);

/*!
 * Returns the current state.
 */
ha_discovery_state_t ha_discovery_manager_get_state(ha_discovery_manager_t* self);

/*!
 * Override the time source (defaults to esphome::millis).
 * Useful for testing.
 */
void ha_discovery_manager_set_time_fn(
  ha_discovery_manager_t* self,
  uint32_t (*get_time_ms)(void));

#ifdef __cplusplus
}
#endif

#endif
