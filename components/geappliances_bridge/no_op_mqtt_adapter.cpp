/*!
 * @file
 * @brief No-op MQTT client adapter implementation.
 */

#include "no_op_mqtt_adapter.h"
#include "esphome/core/log.h"

extern "C" {
#include "tiny_utils.h"
}

#include <cstdio>

static const char* const TAG __attribute__((unused)) = "no_op_mqtt";

static void register_erd(i_mqtt_client_t* _self, tiny_erd_t erd)
{
  auto self = reinterpret_cast<no_op_mqtt_adapter_t*>(_self);
  self->erd_registrations++;
  ESP_LOGI(TAG, "ERD registered: 0x%04X (total: %zu)", (unsigned)erd, self->erd_registrations);
  (void)erd;
}

static void update_erd(i_mqtt_client_t* _self, tiny_erd_t erd, const void* value, uint8_t size)
{
  auto self = reinterpret_cast<no_op_mqtt_adapter_t*>(_self);
  self->erd_updates++;

  // Log the hex payload inline
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value);
  char hex[256];
  size_t hex_len = 0;
  for (uint8_t i = 0; i < size && hex_len < sizeof(hex) - 2; i++) {
    hex_len += snprintf(hex + hex_len, sizeof(hex) - hex_len, "%02X", bytes[i]);
  }
  hex[hex_len] = '\0';

  ESP_LOGI(TAG, "ERD update: 0x%04X value=%s len=%u (total: %zu)", (unsigned)erd, hex, size, self->erd_updates);
  (void)erd; (void)value; (void)size;
}

static void update_erd_write_result(
  i_mqtt_client_t* _self,
  tiny_erd_t erd,
  bool success,
  tiny_gea3_erd_client_write_failure_reason_t failure_reason)
{
  auto self = reinterpret_cast<no_op_mqtt_adapter_t*>(_self);

  if (success) {
    self->write_successes++;
    ESP_LOGI(TAG, "Write success: 0x%04X (total: %zu)", (unsigned)erd, self->write_successes);
  } else {
    self->write_failures++;
    ESP_LOGW(TAG, "Write failed: 0x%04X reason=%u (total: %zu)", (unsigned)erd, (unsigned)failure_reason, self->write_failures);
  }
  (void)erd; (void)success; (void)failure_reason;
}

static i_tiny_event_t* on_write_request(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<no_op_mqtt_adapter_t*>(_self);
  return &self->on_write_request_event.interface;
}

static i_tiny_event_t* on_mqtt_disconnect(i_mqtt_client_t* _self)
{
  auto self = reinterpret_cast<no_op_mqtt_adapter_t*>(_self);
  return &self->on_mqtt_disconnect_event.interface;
}

static const i_mqtt_client_api_t api = {
  register_erd,
  update_erd,
  update_erd_write_result,
  on_write_request,
  on_mqtt_disconnect
};

extern "C" void no_op_mqtt_adapter_init(no_op_mqtt_adapter_t* self)
{
  self->interface.api = &api;
  self->erd_registrations = 0;
  self->erd_updates = 0;
  self->write_successes = 0;
  self->write_failures = 0;

  tiny_event_init(&self->on_write_request_event);
  tiny_event_init(&self->on_mqtt_disconnect_event);

  ESP_LOGI(TAG, "No-op MQTT adapter initialized - all ERD operations will be logged");
}

extern "C" void no_op_mqtt_adapter_destroy(no_op_mqtt_adapter_t* self)
{
  ESP_LOGI(TAG, "No-op MQTT adapter stats: %zu registrations, %zu updates, %zu write successes, %zu write failures",
           self->erd_registrations, self->erd_updates,
           self->write_successes, self->write_failures);
  (void)self;
}

extern "C" size_t no_op_mqtt_adapter_get_erd_registrations(const no_op_mqtt_adapter_t* self)
{
  return self->erd_registrations;
}

extern "C" size_t no_op_mqtt_adapter_get_erd_updates(const no_op_mqtt_adapter_t* self)
{
  return self->erd_updates;
}

extern "C" size_t no_op_mqtt_adapter_get_write_successes(const no_op_mqtt_adapter_t* self)
{
  return self->write_successes;
}

extern "C" size_t no_op_mqtt_adapter_get_write_failures(const no_op_mqtt_adapter_t* self)
{
  return self->write_failures;
}
