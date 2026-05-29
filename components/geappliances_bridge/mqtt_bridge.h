/*!
 * @file
 * @brief Pushes published ERDs to and fulfills write requests from an MQTT server.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Subscribe to all ERDs at the appliance's address and relay their
//       published values to MQTT; fulfill write commands received from MQTT.
//
// Responsibilities:
//   - Manage a tiny_hsm that drives GEA3 subscription lifecycle
//   - Forward ERD publications to i_mqtt_client_t
//   - Route write commands from i_mqtt_client_t back to the ERD client
//
// NOT responsible for:
//   - Polling (see mqtt_bridge_polling.h)
//   - Deciding which ERDs to publish (filtered upstream by i_mqtt_client)
//   - Bridge initialization or startup phase management
//
// Dependencies:
//   - i_mqtt_client.h, i_tiny_gea3_erd_client.h, tiny_hsm.h, tiny_timer.h
// =============================================================================

#ifndef mqtt_bridge_h
#define mqtt_bridge_h

#include "i_mqtt_client.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"

typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  tiny_timer_t timer;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  void* erd_set;
  tiny_hsm_t hsm;
  uint8_t erd_host_address;
} mqtt_bridge_t;

/*!
 * Initialize the MQTT bridge.
 */
void mqtt_bridge_init(
  mqtt_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint8_t address);

/*!
 * Destroy the MQTT bridge.
 */
void mqtt_bridge_destroy(
  mqtt_bridge_t* self);

#endif
