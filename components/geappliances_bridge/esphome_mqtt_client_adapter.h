// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Implement the i_mqtt_client_t interface for the bridge, providing
//       debug logging of ERD value updates without requiring an MQTT broker.
//
// Responsibilities:
//   - Implement i_mqtt_client_t for the bridge and polling bridge
//   - Log ERD updates via ESP_LOGD for debugging and development
//   - Provide no-op implementations for MQTT-specific operations
//
// NOT responsible for:
//   - Deciding which ERDs to publish (filtering is applied via ErdRegistry)
//   - Managing bridge lifecycle or startup phases
//   - HA discovery publishing (HaDiscoveryManager)
//
// Dependencies:
//   - i_mqtt_client.h (interface implemented here)
//   - ErdRegistry for valid-ERD filtering
// =============================================================================

#pragma once

#include <string>

#include "erd_registry.h"

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}


typedef struct {
  i_mqtt_client_t interface;
  std::string* device_id;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;
  // Optional ERD registry: when non-null, provides valid-ERD filtering,
  // string-ERD type detection, and registered-ERD tracking in one place.
  // Set via esphome_mqtt_client_adapter_set_erd_registry().
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
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
 * No-op: write command subscriptions are not available without MQTT.
 */
void esphome_mqtt_client_adapter_subscribe_write_topic(
  esphome_mqtt_client_adapter_t* self);

/*!
 * No-op: returns 0 (no pending updates).
 */
size_t esphome_mqtt_client_adapter_drain_pending_updates(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);

size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self);

/*!
 * Log a publish message via ESP_LOGD.  Used by HA discovery manager.
 */
void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* self,
  const std::string& topic,
  const std::string& payload,
  bool retain);

#ifdef __cplusplus
}
#endif