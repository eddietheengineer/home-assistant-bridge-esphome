/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Serialized producer-consumer design:
 *   Producer (background task): decompresses embedded JSONL entity definitions,
 *   builds one discovery topic/payload at a time in a shared buffer, then
 *   signals the consumer via a binary semaphore.
 *
 *   Consumer (main loop run()): receives the semaphore, publishes the payload
 *   to MQTT, releases the semaphore, and the producer continues.
 *
 *   This eliminates the item pool entirely — only one payload is in flight at
 *   any time, minimizing RAM usage.
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
#    include "miniz.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Discovery manager states */
typedef enum {
  ha_discovery_state_idle,
  ha_discovery_state_fetching,    // fetch task running
  ha_discovery_state_publishing,  // main loop publishing
  ha_discovery_state_complete,
  ha_discovery_state_failed
} ha_discovery_state_t;

/* Maximum number of registered/seen ERDs for HA discovery binary search. */
#define HA_DISCOVERY_MAX_ERDS 645

/* Publish rate limit interval in milliseconds. */
#define HA_DISCOVERY_PUBLISH_INTERVAL_MS 50

/* Decompression buffer size per chunk (max chunk is ~14KB). */
#define HA_DISCOVERY_DECOMP_BUF_SIZE 16384

/* Line buffer size for JSONL parsing (max line is ~14KB). */
#define HA_DISCOVERY_LINE_BUF_SIZE 16384

/* Shared payload buffer (single item in flight). */
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 16384

/*!
 * @brief Home Assistant MQTT Discovery manager.
 *
 * All buffers are pre-allocated — no heap allocation during processing.
 * Peak memory: payload buffer (~16 KB) + decompress buffer (~16 KB) +
 * line buffer (~16 KB) + sorted ERD array (~1.3 KB).
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
  /* Fetch task resources (heap-allocated for stack/TCB). */
  TaskHandle_t    task_handle;
  StackType_t*    task_stack;      // heap-allocated (8KB or 4KB fallback)
  StaticTask_t*   task_tcb;        // heap-allocated
  bool task_running;

  /* Serialized producer-consumer sync.
   * publish_sem: binary semaphore. Producer takes before building payload,
   *   gives after. Consumer takes to claim the payload, gives after publishing.
   * done_sem: given by producer when all categories are processed.
   */
  SemaphoreHandle_t publish_sem;
  SemaphoreHandle_t done_sem;

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

  /* Shared payload buffer: producer builds, consumer reads. */
  char topic_buf[128];
  char payload_buf[HA_DISCOVERY_PAYLOAD_BUF_SIZE];

  /* Rate limiting for consumer. */
  uint32_t last_publish_ms;

  /* Device JSON built once at fetch start. */
  char device_json_buf[512];

  /* Entity field buffers (used by process_jsonl_line to avoid stack overflow).
   * Templates are NOT stored here — they are embedded directly from the raw
   * JSONL line into the payload buffer with proper re-escaping. */
  char entity_name_buf[128];
  char erd_id_hex_buf[8];
  char domain_buf[32];
  char field_id_buf[16];
  char paired_erd_buf[8];
  char role_buf[16];
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
 * Drive the consumer: wait for payload from producer, publish to MQTT.
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
