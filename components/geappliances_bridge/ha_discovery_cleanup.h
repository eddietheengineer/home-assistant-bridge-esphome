/**
 * HA Discovery Cleanup Module.
 * Discovers and removes old Home Assistant MQTT discovery topics for a device.
 * Independent of the discovery manager — no knowledge of discovery state or
 * buffers.
 */

#ifndef ha_discovery_cleanup_h
#define ha_discovery_cleanup_h

#include <stdint.h>
#include <stdbool.h>


#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#include "i_mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* States */

typedef enum {
  ha_cleanup_state_idle,
  ha_cleanup_state_cleaning,
  ha_cleanup_state_done,
} ha_cleanup_state_t;

/* Constants */

#define HA_CLEANUP_IDLE_TIMEOUT_MS 1000
#define HA_CLEANUP_MIN_SUBSCRIBE_MS 1000
#define HA_CLEANUP_DRAIN_WAIT_MS 1000
#define HA_CLEANUP_FLUSH_BATCH 1

/* Buffer size (overridable for tests) */

#ifdef HA_CLEANUP_TEST_BUF_SIZE
  #define HA_CLEANUP_TOPIC_BUF_SIZE HA_CLEANUP_TEST_BUF_SIZE
#else
  #define HA_CLEANUP_TOPIC_BUF_SIZE 6144
#endif

/* Context */

typedef struct {
  i_mqtt_client_t* mqtt_client;
  const char* device_id;
  ha_cleanup_state_t state;
  uint32_t (*get_time_ms)(void);

  /* Topic queue — ring buffer */
  char topic_buf[HA_CLEANUP_TOPIC_BUF_SIZE];
  uint16_t queue_write_pos;
  uint16_t queue_count;
  uint16_t dropped_count;

  /* Timing */
  uint32_t last_activity_ms;
  uint32_t subscribe_start_ms;
  bool subscribed;
  bool flushed_once;
  uint8_t clean_passes;
  bool pass_found_topics;
  uint16_t pass_received_count;
  uint16_t pass_removed_count;
  uint8_t pass_number;

  /* Drain */
  uint32_t drain_start_ms;
} ha_discovery_cleanup_t;

/* API */

void ha_discovery_cleanup_init(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_configure(ha_discovery_cleanup_t* self,
    const char* device_id, i_mqtt_client_t* mqtt_client, uint32_t (*get_time_ms)(void));

void ha_discovery_cleanup_start(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_run(ha_discovery_cleanup_t* self);

void ha_discovery_cleanup_destroy(ha_discovery_cleanup_t* self);

ha_cleanup_state_t ha_discovery_cleanup_get_state(ha_discovery_cleanup_t* self);

bool ha_discovery_cleanup_is_done(ha_discovery_cleanup_t* self);

/* Test exports */

#ifdef HA_DISCOVERY_CLEANUP_TEST_EXPORT
void cleanup_topic_callback(const char* topic, const char* payload, size_t payload_len, void* arg);
uint16_t cleanup_flush_queue(ha_discovery_cleanup_t* self);
void cleanup_start(ha_discovery_cleanup_t* self);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ha_discovery_cleanup_h */
