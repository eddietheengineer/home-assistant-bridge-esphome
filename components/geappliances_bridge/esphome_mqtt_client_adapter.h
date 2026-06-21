// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Implement the i_mqtt_client_t interface for the bridge, publishing
//       ERD value updates to MQTT topics via ESPHome's global MQTT client.
//
// Responsibilities:
//   - Implement i_mqtt_client_t for the bridge and polling bridge
//   - Publish ERD updates to geappliances/{device_id}/erd/0x{ERD}/value topics
//   - Provide MQTT connect/disconnect events for publisher coordination
//
// NOT responsible for:
//   - Deciding which ERDs to publish (filtering is applied via ErdRegistry)
//   - Managing bridge lifecycle or startup phases
//   - HA discovery publishing (HaDiscoveryManager)
//
// Dependencies:
//   - i_mqtt_client.h (interface implemented here)
//   - ErdRegistry for valid-ERD filtering
//   - ESPHome MQTT client (esphome::mqtt::global_mqtt_client)
// =============================================================================

#pragma once
#include "esphome/components/mqtt/mqtt_client.h"

#include <functional>
#include <map>
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
  tiny_event_t on_mqtt_connect_event;
  // Optional ERD registry: when non-null, provides valid-ERD filtering,
  // string-ERD type detection, and registered-ERD tracking in one place.
  // Set via esphome_mqtt_client_adapter_set_erd_registry().
  esphome::geappliances_bridge::ErdRegistry* erd_registry;
  // Buffer for decoded hex payload from write topic.
  // Max ERD write payload is 32 bytes (64 hex chars).
  uint8_t write_payload_buffer_[32];
  uint8_t write_payload_size_;
  // Subscription tracking for esphome_mqtt_client_adapter_subscribe/unsubscribe.
  // Stores C callback + user_data instead of std::function to avoid heap allocation.
  struct MqttSubscription {
    std::string topic;
    void (*callback)(const char* topic, const char* payload, size_t payload_len, void* user_data);
    void* user_data;
  };
  std::map<uint16_t, MqttSubscription> subscriptions_;
  uint16_t next_subscription_handle_{1};
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
 * Returns true if the MQTT broker is currently connected.
 */
bool esphome_mqtt_client_adapter_is_connected(
  const esphome_mqtt_client_adapter_t* self);

/*!
 * Publish an MQTT message.  Used by HA discovery manager.
 */
void esphome_mqtt_client_adapter_publish(
  esphome_mqtt_client_adapter_t* self,
  const std::string& topic,
  const std::string& payload,
  bool retain);

/*!
 * Publish raw MQTT message (C-string topic and payload).
 * Implements the i_mqtt_client_t publish_raw vtable slot.
 */
void esphome_mqtt_client_adapter_publish_raw(
  i_mqtt_client_t* self,
  const char* topic,
  const char* payload,
  size_t payload_len,
  bool retain);

/*!
 * Subscribe to an MQTT topic with a callback.
 * The callback receives (topic, payload, payload_len, user_data).
 * Returns a handle for later unsubscribe, or 0 on failure.
 */
typedef uint16_t mqtt_subscription_handle_t;
typedef void (*mqtt_subscribe_callback_t)(const char* topic, const char* payload, size_t payload_len, void* user_data);

mqtt_subscription_handle_t esphome_mqtt_client_adapter_subscribe(
  esphome_mqtt_client_adapter_t* self,
  const char* topic,
  mqtt_subscribe_callback_t callback,
  void* user_data);

/*!
 * Unsubscribe from a topic by handle.
 */
void esphome_mqtt_client_adapter_unsubscribe(
  esphome_mqtt_client_adapter_t* self,
  mqtt_subscription_handle_t handle);

#ifdef __cplusplus
}
#endif