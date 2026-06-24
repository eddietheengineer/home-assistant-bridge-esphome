/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Decompresses embedded JSONL entity definitions, filters against the
 * device's registered ERDs, and publishes HA discovery payloads to MQTT.
 *
 * Two-task design (mirrors erd_cache_mqtt_publisher):
 *   1. Decompress/parse task: runs once during start(), decompresses
 *      embedded JSONL, parses, filters against ERD cache, and queues
 *      discovery payloads into a pre-allocated ring buffer.
 *   2. MQTT publish task: long-lived background task that drains the
 *      ring buffer and publishes discovery payloads at 50ms intervals.
 *      Signaled from loop() via signal_work().
 *
 * States: IDLE -> DECOMPRESSING -> PUBLISHING -> COMPLETE / FAILED
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
#    include "freertos/queue.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Discovery manager states */
typedef enum {
  ha_discovery_state_idle,
  ha_discovery_state_decompressing,
  ha_discovery_state_publishing,
  ha_discovery_state_complete,
  ha_discovery_state_failed
} ha_discovery_state_t;

/*
 * Pre-allocated discovery payload item.
 * Each item holds a fully-formed MQTT topic and JSON payload
 * ready for publishing.
 */
#define HA_DISCOVERY_TOPIC_SIZE 128
#define HA_DISCOVERY_PAYLOAD_SIZE 512

typedef struct {
  char topic[HA_DISCOVERY_TOPIC_SIZE];
  char payload[HA_DISCOVERY_PAYLOAD_SIZE];
} ha_discovery_item_t;

/* Maximum number of discovery items that can be queued. */
#define HA_DISCOVERY_QUEUE_CAPACITY 256

/*!
 * @brief Home Assistant MQTT Discovery manager.
 *
 * All buffers are pre-allocated — no heap allocation during decompress
 * or publish phases.
 */
typedef struct {
  erd_cache_t* cache;              // Shared ERD cache (owned by GeappliancesBridge)
  i_mqtt_client_t* mqtt_client;    // MQTT publish interface
  const char* device_id;           // Device ID string for topic construction
  const char* model_number;        // Model number for device info
  const char* serial_number;       // Serial number for device info

  ha_discovery_state_t state;

  /* Pre-allocated queue for discovery items. */
  ha_discovery_item_t queue[HA_DISCOVERY_QUEUE_CAPACITY];
  uint16_t queue_head;             // Next item to publish
  uint16_t queue_tail;             // Next slot to enqueue
  uint16_t queue_count;            // Items currently in queue

  /* Stats */
  uint32_t total_discovered;       // Total entities discovered
  uint32_t total_published;        // Total discovery publishes
  uint32_t total_filtered;         // Entities filtered out (ERD not registered)

  /* Pre-allocated buffers for the background task. */
  char task_topic[HA_DISCOVERY_TOPIC_SIZE];

  /* Time source */
  uint32_t (*get_time_ms)(void);

#ifdef USE_ESP_IDF
  TaskHandle_t    decompress_task_handle;
  StaticTask_t    decompress_task_tcb;
  StackType_t     decompress_task_stack[2048 / sizeof(StackType_t)];

  TaskHandle_t    publish_task_handle;
  StaticTask_t    publish_task_tcb;
  StackType_t     publish_task_stack[2048 / sizeof(StackType_t)];

  SemaphoreHandle_t work_semaphore;
  SemaphoreHandle_t done_semaphore;
  SemaphoreHandle_t queue_mutex;  // Protects queue from torn reads
  bool task_running;

  /* Pre-allocated buffers for the decompress task. */
  char decompress_line[512];
  char decompress_topic[HA_DISCOVERY_TOPIC_SIZE];
  char decompress_payload[HA_DISCOVERY_PAYLOAD_SIZE];
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
 * Decompresses embedded JSONL, parses, filters, and queues discovery payloads.
 * On ESP-IDF, spawns background tasks; on non-ESP-IDF, runs inline.
 */
void ha_discovery_manager_start(ha_discovery_manager_t* self);

/*!
 * Clean up the discovery manager.
 * Stops tasks and frees resources. Call from teardown.
 */
void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);

/*!
 * Signal the background publish task that there is work to do (ESP-IDF only).
 * On non-ESP-IDF, call run() directly from loop().
 */
void ha_discovery_manager_signal_work(ha_discovery_manager_t* self);

/*!
 * Inline drain for non-ESP-IDF platforms.
 * Publishes one discovery item per call, up to max_publishes.
 * Returns the number of items published.
 */
uint16_t ha_discovery_manager_run(
  ha_discovery_manager_t* self,
  uint16_t max_publishes);

/*!
 * Returns true if the manager is in the PUBLISHING state.
 */
bool ha_discovery_manager_is_publishing(ha_discovery_manager_t* self);

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
