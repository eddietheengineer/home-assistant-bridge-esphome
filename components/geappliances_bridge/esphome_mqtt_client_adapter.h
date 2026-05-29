// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Deliver ERD value updates to the MQTT broker reliably and
//       asynchronously, with deduplication and reconnect-safe queuing.
//
// Responsibilities:
//   - Implement i_mqtt_client_t for the bridge and polling bridge
//   - Publish ERD updates asynchronously via a FreeRTOS task queue
//   - Deduplicate pending updates per ERD to bound memory use
//   - Re-flush pending updates on MQTT reconnect
//   - Route wildcard write-command subscriptions back to the ERD client
//
// NOT responsible for:
//   - Deciding which ERDs to publish (filtering is applied via ErdRegistry)
//   - Managing bridge lifecycle or startup phases
//   - HA discovery publishing (HaDiscoveryManager)
//
// Dependencies:
//   - esphome::mqtt::MQTTClientComponent
//   - i_mqtt_client.h (interface implemented here)
//   - FreeRTOS task + queue on ESP-IDF builds
// =============================================================================

#pragma once

#include <string>
#include <map>
#include <set>

#include "erd_registry.h"

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}

struct PendingErdUpdate {
  std::string topic;
  std::string payload;
};

typedef struct {
  i_mqtt_client_t interface;
  std::string* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  // Keyed by ERD so repeated updates while MQTT is down keep only the latest
  // value per ERD. This prevents the queue from filling with duplicates during
  // a polling reconnect cycle and bounds its size to the number of distinct ERDs.
  std::map<tiny_erd_t, PendingErdUpdate>* pending_updates;
  // Optional ERD registry: when non-null, provides valid-ERD filtering,
  // string-ERD type detection, and registered-ERD tracking in one place.
  // Set via esphome_mqtt_client_adapter_set_erd_registry().
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
  // True once the single wildcard MQTT subscription for write commands has been
  // established.  Set on the first MQTT connect after adapter init; never
  // cleared, because ESPHome's MQTT client automatically re-subscribes all
  // registered topics on reconnect, so we only need to call subscribe() once.
  bool wildcard_subscribed;
  // millis() timestamp of the most recent MQTT connection (set on the first
  // notify_connected() call after each disconnect; reset to 0 by
  // notify_disconnected()).  Used to gate the pending-update flush so the IDF
  // MQTT task has time to process the broker's reconnect backlog.
  uint32_t mqtt_connected_at_ms;
} esphome_mqtt_client_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_mqtt_client_adapter_init(
  esphome_mqtt_client_adapter_t* self,
  const char* device_id);

void esphome_mqtt_client_adapter_set_erd_registry(
  esphome_mqtt_client_adapter_t* self,
  esphome::geappliances_bridge::ErdRegistry* erd_registry);

void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self);

/*!
 * Subscribe the single wildcard write topic (geappliances/{id}/erd/+/write).
 * Idempotent — does nothing after the first successful subscribe.  Records
 * mqtt_connected_at_ms on first call after each reconnect.
 * Called by the bridge MQTT FSM in the SUBSCRIBING state.
 */
void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self);

/*!
 * Flush up to MAX_FLUSH_PER_CALL pending ERD updates to the broker.
 * Returns the number of updates still pending after this call; 0 means
 * the queue is empty.  Called by the bridge MQTT FSM in FLUSHING / RUNNING.
 */
size_t esphome_mqtt_client_adapter_drain_pending_updates(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);

size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self);

/*!
 * Enqueue a publish request for the async MQTT publish task.  Non-blocking
 * on ESP-IDF (drops with warning if queue is full).  Falls back to
 * synchronous publish on non-ESP-IDF builds.
 */
void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* self,
  const std::string& topic,
  const std::string& payload,
  bool retain);

#ifdef __cplusplus
}
#endif