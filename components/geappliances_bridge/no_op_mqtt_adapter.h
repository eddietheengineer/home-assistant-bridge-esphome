/*!
 * @file
 * @brief No-op MQTT client adapter for appliance-side stability testing.
 *
 * Replaces the real MQTT adapter entirely.  Every ERD registration, value
 * update, and write result is logged via ESP_LOG and counted — nothing is
 * published to a broker.  The appliance-side protocol stack (GEA2/GEA3),
 * autodiscovery, feature bits, polling, and subscription bridges run
 * identically to normal operation.
 *
 * Responsibilities:
 *   - Implement i_mqtt_client_t so the bridges compile and link unchanged
 *   - Log every ERD operation (register, update, write result)
 *   - Track counters: registrations, updates, write successes, write failures
 *   - Provide no-op events for write_request and mqtt_disconnect
 *
 * NOT responsible for:
 *   - Any network I/O
 *   - HA discovery publishing
 *   - Topic construction or payload encoding
 */

#ifndef no_op_mqtt_adapter_h
#define no_op_mqtt_adapter_h

extern "C" {
#include "i_mqtt_client.h"
#include "tiny_event.h"
}

typedef struct {
  i_mqtt_client_t interface;
  tiny_event_t on_write_request_event;
  tiny_event_t on_mqtt_disconnect_event;

  // Counters
  size_t erd_registrations;
  size_t erd_updates;
  size_t write_successes;
  size_t write_failures;
} no_op_mqtt_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void no_op_mqtt_adapter_init(no_op_mqtt_adapter_t* self);

void no_op_mqtt_adapter_destroy(no_op_mqtt_adapter_t* self);

size_t no_op_mqtt_adapter_get_erd_registrations(const no_op_mqtt_adapter_t* self);
size_t no_op_mqtt_adapter_get_erd_updates(const no_op_mqtt_adapter_t* self);
size_t no_op_mqtt_adapter_get_write_successes(const no_op_mqtt_adapter_t* self);
size_t no_op_mqtt_adapter_get_write_failures(const no_op_mqtt_adapter_t* self);

#ifdef __cplusplus
}
#endif

#endif
