/*!
 * @file
 * @brief Scans the shared ERD cache each loop() and publishes updated ERDs
 *        to MQTT topics with retain=true.
 *
 * Responsibilities:
 *   - Iterate cache entries with update_required=true
 *   - Publish to geappliances/{deviceId}/erd/0x{ERD:04X}/value
 *   - Respect time budget and hard cap per loop() call
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
  // Time callback (defaults to esphome::millis; overridable for testing)
  uint32_t (*get_time_ms)(void);
  // When true, publish every valid cache entry each loop pass (not just
  // those with update_required=true).  Used when
  // polling_only_publish_on_change is disabled.
  bool publish_all;
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
 * Returns the number of ERDs actually published.
 * No-ops if MQTT is disconnected (increments missed_loops).
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
 * Set whether to publish all valid cache entries each loop pass.
 * When true (polling_only_publish_on_change=false), every valid entry
 * is published regardless of whether its data changed.
 * When false (polling_only_publish_on_change=true), only entries with
 * update_required=true are published (the default).
 */
void erd_cache_mqtt_publisher_set_publish_all(
  erd_cache_mqtt_publisher_t* self,
  bool publish_all);

/*!
 * Override the time source (defaults to esphome::millis).
 * Useful for testing.
 */
void erd_cache_mqtt_publisher_set_time_fn(
  erd_cache_mqtt_publisher_t* self,
  uint32_t (*get_time_ms)(void));

#ifdef __cplusplus
}
#endif

#endif
