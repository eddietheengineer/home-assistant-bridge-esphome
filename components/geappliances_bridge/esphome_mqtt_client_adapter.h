#pragma once

#include <string>
#include <map>
#include <set>

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
  // Optional filter: when non-null, update_erd only publishes ERDs that are
  // present in this set. Used when appliance_api_parsing is enabled.
  const std::set<tiny_erd_t>* valid_erds_filter;
  // Optional set of string-type ERDs: when an ERD is in this set, update_erd
  // publishes the raw bytes as a null-terminated ASCII string instead of hex.
  const std::set<tiny_erd_t>* string_erds_filter;
  // Optional output set: when non-null, every ERD passed to register_erd() is
  // added here so the bridge can track which ERDs the device has registered.
  std::set<tiny_erd_t>* registered_erds_out;
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

void esphome_mqtt_client_adapter_set_valid_erds_filter(
  esphome_mqtt_client_adapter_t* self,
  const std::set<tiny_erd_t>* valid_erds_filter);

void esphome_mqtt_client_adapter_set_string_erds_filter(
  esphome_mqtt_client_adapter_t* self,
  const std::set<tiny_erd_t>* string_erds_filter);

void esphome_mqtt_client_adapter_set_registered_erds_out(
  esphome_mqtt_client_adapter_t* self,
  std::set<tiny_erd_t>* registered_erds_out);

void esphome_mqtt_client_adapter_notify_disconnected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_notify_connected(
  esphome_mqtt_client_adapter_t* self);

void esphome_mqtt_client_adapter_destroy(
  esphome_mqtt_client_adapter_t* self);

size_t esphome_mqtt_client_adapter_get_pending_update_count(
  const esphome_mqtt_client_adapter_t* self);

#ifdef __cplusplus
}
#endif
