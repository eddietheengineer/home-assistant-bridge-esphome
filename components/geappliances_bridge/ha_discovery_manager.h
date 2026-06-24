/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Producer-consumer design:
 *   Producer (background task): decompresses embedded JSONL entity definitions,
 *   builds discovery topic/payload pairs, and queues item indices via a FreeRTOS
 *   queue.  Uses a pre-allocated item pool to avoid heap allocation.
 *
 *   Consumer (main loop run()): drains the queue at 50 ms intervals, publishes
 *   each entity to MQTT, and feeds the WDT.  This decouples decompression from
 *   publishing, preventing MQTT mutex starvation.
 *
 * States: IDLE -> FETCHING -> PUBLISHING -> COMPLETE / FAILED
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
#    include "miniz.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Discovery manager states */
typedef enum {
  ha_discovery_state_idle,
  ha_discovery_state_fetching,    // fetch task running (decompressing/queueing)
  ha_discovery_state_publishing,  // main loop draining queue
  ha_discovery_state_complete,
  ha_discovery_state_failed
} ha_discovery_state_t;

/* Pre-allocated (topic, payload) pair for the item pool. */
typedef struct {
  char topic[128];
  char payload[1024];
} ha_discovery_item_t;

/* Sentinel value for the queue: signals fetch task completion. */
#define HA_DISCOVERY_QUEUE_SENTINEL 0xFFFF

/* Maximum number of registered/seen ERDs for HA discovery binary search. */
#define HA_DISCOVERY_MAX_ERDS 645

/* Number of pre-allocated items in the pool (queue depth). */
#define HA_DISCOVERY_ITEM_POOL_SIZE 32

/* Publish rate limit interval in milliseconds. */
#define HA_DISCOVERY_PUBLISH_INTERVAL_MS 50

/* Decompression buffer size per chunk. */
#define HA_DISCOVERY_DECOMP_BUF_SIZE 4096

/* Line buffer size for JSONL parsing. */
#define HA_DISCOVERY_LINE_BUF_SIZE 4096

/*!
 * @brief Home Assistant MQTT Discovery manager.
 *
 * All buffers are pre-allocated — no heap allocation during processing.
 * Peak memory: item pool (~37 KB) + decompress buffer (~4 KB) +
 * sorted ERD array (~1.3 KB).
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
  /* Fetch task resources (heap-allocated for stack/TCB, static for queue). */
  TaskHandle_t    task_handle;
  StackType_t*    task_stack;      // heap-allocated (8KB or 4KB fallback)
  StaticTask_t*   task_tcb;        // heap-allocated
  SemaphoreHandle_t done_semaphore;
  bool task_running;

  /* Producer-consumer queue: fetch task sends uint16_t indices into item_pool_. */
  QueueHandle_t item_queue;

  /* Pre-allocated pool of (topic, payload) pairs. */
  ha_discovery_item_t item_pool[HA_DISCOVERY_ITEM_POOL_SIZE];
  uint16_t item_pool_next;         // round-robin index into item_pool_

  /* Fetch state. */
  bool fetch_done;                 // true once fetch task terminates

  /* Sorted ERD array for binary search during fetch. */
  uint16_t sorted_erds[HA_DISCOVERY_MAX_ERDS];
  uint16_t sorted_erds_count;

  /* Decompression state (direct member, no raw buffer cast). */
  tinfl_decompressor decomp_state;
  uint8_t decomp_buf[HA_DISCOVERY_DECOMP_BUF_SIZE];

  /* Line parsing buffer. */
  char line_buf[HA_DISCOVERY_LINE_BUF_SIZE];
  /* Payload building buffer (used by process_jsonl_line). */
  char payload_buf[1024];

  /* Rate limiting for consumer. */
  uint32_t last_publish_ms;

  /* Device JSON built once at fetch start. */
  char device_json_buf[512];
  /* Entity field buffers (used by process_jsonl_line to avoid stack overflow). */
  char entity_name_buf[128];
  char domain_buf[32];
  char field_id_buf[16];
  char paired_erd_buf[8];
  char role_buf[16];
  char value_template_buf[512];
  char command_template_buf[512];
  char unit_buf[32];
  char device_class_buf[32];
  char state_class_buf[32];
  char options_buf[256];
  char data_type_buf[16];
  char scale_factor_buf[16];
  char unique_id_buf[128];
  char state_topic_buf[128];
  char command_topic_buf[128];
  char actual_state_topic_buf[128];
  char actual_command_topic_buf[128];
  char topic_buf[128];
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
 * On ESP-IDF, spawns a background fetch task and transitions to FETCHING.
 * On non-ESP-IDF, marks complete immediately.
 */
void ha_discovery_manager_start(ha_discovery_manager_t* self);

/*!
 * Drive the consumer: drain the queue and publish at rate-limited intervals.
 * Call from the main loop while the manager is in FETCHING or PUBLISHING state.
 * Transitions to COMPLETE when all items are published.
 */
void ha_discovery_manager_run(ha_discovery_manager_t* self);

/*!
 * Clean up the discovery manager.
 * Stops tasks and frees resources. Call from teardown.
 */
void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);

/*!
 * Returns true if the manager is currently processing (fetching or publishing).
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
