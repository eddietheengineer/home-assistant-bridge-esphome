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
#    include "miniz_tinfl.h"
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
#define HA_DISCOVERY_DECOMP_BUF_SIZE 14336

/* Line buffer size for JSONL parsing (max line is ~14KB). */
#define HA_DISCOVERY_LINE_BUF_SIZE 14336

/* Payload buffer for building discovery payloads. */
#define HA_DISCOVERY_PAYLOAD_BUF_SIZE 8192

/* Home Assistant domain strings for topic reconstruction during cleanup.
 * Used to pack/unpack topics in the compacting cleanup buffer. */
#define HA_DOMAIN_COUNT 21

static const char* const HA_DOMAIN_STRINGS[HA_DOMAIN_COUNT] = {
    "alarm_control_panel", "binary_sensor", "button", "camera", "climate",
    "cover", "date", "datetime", "event", "fan", "light", "lock", "number",
    "select", "sensor", "switch", "text", "time", "update", "vacuum", "valve"
};

static inline int ha_domain_to_index(const char* str, size_t len) {
    for (int i = 0; i < HA_DOMAIN_COUNT; i++) {
        if (strlen(HA_DOMAIN_STRINGS[i]) == len &&
            strncmp(HA_DOMAIN_STRINGS[i], str, len) == 0) {
            return i;
        }
    }
    return -1;
}

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
  char topic_buf[192];
  char payload_buf[HA_DISCOVERY_PAYLOAD_BUF_SIZE];

  /* Rate limiting. */
  uint32_t last_publish_ms;

  /* Device JSON built once at start. */
  char device_json_buf[512];

  /* Entity field buffers (used by process_jsonl_line to avoid stack overflow).
   * Templates are NOT stored here — they are embedded directly from the raw
   * JSONL line into the payload buffer with proper re-escaping. */
  char entity_name_buf[160];
  char erd_id_hex_buf[8];
  char domain_buf[32];
  char field_id_buf[72];
  char paired_erd_buf[8];
  char role_buf[16];
  char unit_buf[32];
  char device_class_buf[32];
  char state_class_buf[32];
  char options_buf[256];
  char data_type_buf[16];
  char scale_factor_buf[16];
  char min_buf[32];
  char max_buf[32];
  char step_buf[32];
  char mode_buf[16];
  char payload_on_buf[16];
  char payload_off_buf[16];
  char state_on_buf[16];
  char state_off_buf[16];
  char unique_id_buf[160];
  char state_topic_buf[128];
  char command_topic_buf[128];
  char actual_state_topic_buf[128];
  char actual_command_topic_buf[128];

  /* Discovery progress tracking. */
  uint16_t current_category;       // Index into ha_discovery_categories[]
  uint16_t current_chunk;          // Index into current category's chunks
  uint32_t current_offset;         // Byte offset within decompressed chunk
  uint32_t current_decomp_size;    // Size of current decompressed chunk

  /* Cleanup state: discover and remove old discovery topics.
   * Subscribes to each domain separately (homeassistant/{domain}/{device_id}/#)
   * to limit retained message bursts per subscription. */
  uint32_t cleanup_last_activity_ms;  // Last time a topic was received
  bool cleanup_subscribed;            // Whether we've subscribed to current domain
  uint8_t cleanup_current_domain;     // Current domain index being cleaned (0-20)
  uint8_t cleanup_clean_passes;       // Consecutive passes with no topics found
  bool cleanup_pass_found_topics;     // Whether any topics were found during current pass
  uint16_t cleanup_pass_received_count;  // Topics received by callback during current pass
  uint16_t cleanup_pass_removed_count;   // Topics removed during current pass
  uint8_t cleanup_pass_number;        // Current pass number (starts at 1)
  uint32_t cleanup_wait_start_ms;     // Start time of final wait before discovery

  /* Post-unsubscribe drain tracking: records when we unsubscribed so we can
   * wait for the inbound MQTT event queue to fully drain before re-subscribing.
   * Non-zero means we're in a drain-wait phase. */
  uint32_t cleanup_drain_start_ms;     // Time of last unsubscribe (0 = not draining)

  /* Cleanup yield counter: yields every N publishes during cleanup
   * to let the MQTT inbound queue drain. */
  uint8_t cleanup_yield_counter;

  /* Domain topic prefix: pre-computed "homeassistant/{domain}/{device_id}/"
   * to avoid repeated snprintf during discovery publish. */
  char domain_topic_prefix[128];
  char current_domain_prefix_buf[32]; // Tracks current domain for prefix caching

  /* Yield counter: yields every N published entities during discovery. */
  uint8_t publish_yield_counter;
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

/* Test-only exports: exposed when HA_DISCOVERY_TEST_EXPORT is defined. */
#ifdef HA_DISCOVERY_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
void cleanup_start(ha_discovery_manager_t* self);
uint16_t cleanup_flush_queue(ha_discovery_manager_t* self);
#endif

#ifdef __cplusplus
}
#endif

#endif
