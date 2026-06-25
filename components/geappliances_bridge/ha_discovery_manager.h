/*!
 * @file
 * @brief Home Assistant MQTT Discovery manager.
 *
 * Sequential design: decompresses one chunk at a time into the shared buffer,
 * parses each line, and publishes valid entities with rate limiting. Once all
 * valid entities from a chunk are published, it decompresses the next chunk
 * into the same memory space.
 *
 * States: IDLE -> BUILDING -> DISCOVERING -> COMPLETE / FAILED
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
  ha_discovery_state_building,     // building sorted ERD list
  ha_discovery_state_cleaning,    // removing old discovery topics
  ha_discovery_state_discovering,  // main loop decompressing/publishing
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

/* Payload buffer for building discovery payloads. */
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
  uint8_t appliance_type;          // Appliance type for category filtering

  ha_discovery_state_t state;

  /* Stats */
  uint32_t total_discovered;       // Total entities discovered
  uint32_t total_published;        // Total discovery publishes
  uint32_t total_filtered;         // Entities filtered out (ERD not registered)

  /* Time source */
  uint32_t (*get_time_ms)(void);

#ifdef USE_ESP_IDF
  /* Task resources for initial ERD list build (heap-allocated). */
  TaskHandle_t    task_handle;
  StackType_t*    task_stack;
  StaticTask_t*   task_tcb;
  bool task_running;

  /* Done semaphore: given by build task when sorted ERD list is ready. */
  SemaphoreHandle_t done_sem;

  /* Build state. */
  bool build_done;

  /* Sorted ERD array for binary search during discovery. */
  uint16_t sorted_erds[HA_DISCOVERY_MAX_ERDS];
  uint16_t sorted_erds_count;

  /* Decompression state. */
  tinfl_decompressor decomp_state;
  uint8_t decomp_buf[HA_DISCOVERY_DECOMP_BUF_SIZE];

  /* Line parsing buffer. */
  char line_buf[HA_DISCOVERY_LINE_BUF_SIZE];

  /* Payload buffer for building discovery payloads. */
  char topic_buf[128];
  char payload_buf[HA_DISCOVERY_PAYLOAD_BUF_SIZE];

  /* Rate limiting. */
  uint32_t last_publish_ms;

  /* Device JSON built once at start. */
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
  char mode_buf[16];
  char unique_id_buf[128];
  char state_topic_buf[128];
  char command_topic_buf[128];
  char actual_state_topic_buf[128];
  char actual_command_topic_buf[128];

  /* Discovery progress tracking. */
  uint16_t current_category;       // Index into ha_discovery_categories[]
  uint16_t current_chunk;          // Index into current category's chunks
  uint32_t current_offset;         // Byte offset within decompressed chunk
  uint32_t current_decomp_size;    // Size of current decompressed chunk

  /* Cleanup state: discover and remove old discovery topics. */
  uint32_t cleanup_last_activity_ms;  // Last time a topic was received
  bool cleanup_subscribed;            // Whether we've subscribed
  uint16_t cleanup_current_component; // Index into ha_discovery_component_types[]
  bool cleanup_received_topics;       // Whether we received any topics for current component

  /* Cleanup topic queue: buffer topic names for batched publishing.
   * Each entry is a pointer into the shared topic_buf, so we queue
   * offsets into topic_buf as we receive messages, then publish them
   * in batches from the main loop to avoid blocking the MQTT task. */
#define HA_DISCOVERY_CLEANUP_QUEUE_SIZE 64
  char cleanup_topic_queue[HA_DISCOVERY_CLEANUP_QUEUE_SIZE][128];
  uint16_t cleanup_queue_count;
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
  uint8_t appliance_type,
  erd_cache_t* cache,
  i_mqtt_client_t* mqtt_client);

/*!
 * Start the discovery process.
 * On ESP-IDF, spawns a background build task for the sorted ERD list.
 * On non-ESP-IDF, marks complete immediately.
 */
void ha_discovery_manager_start(ha_discovery_manager_t* self);

/*!
 * Drive the discovery: decompress chunks and publish entities.
 * Call from the main loop while the manager is in BUILDING or DISCOVERING state.
 * Transitions to COMPLETE when all entities are published.
 */
void ha_discovery_manager_run(ha_discovery_manager_t* self);

/*!
 * Clean up the discovery manager.
 * Stops tasks and frees resources. Call from teardown.
 */
void ha_discovery_manager_cleanup(ha_discovery_manager_t* self);

/*!
 * Returns true if the manager is currently processing (building or discovering).
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
