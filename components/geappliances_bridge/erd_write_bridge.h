/*!
 * @file
 * @brief Relays write requests from MQTT to the GEA3 ERD client and reports results back.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Provide a thin relay between MQTT write requests and the GEA3 ERD
//       client, so that the polling and subscription bridges can remain
//       pure data sources (read → cache).
//
// Responsibilities:
//   - Subscribe to mqtt_client_on_write_request
//   - Route each write to the board address stored in the ERD cache for
//     that ERD (primary-board entries resolve to the detected host address)
//   - Forward writes to the ERD client via tiny_gea3_erd_client_write
//   - Report write results back to MQTT via mqtt_client_update_erd_write_result
//   - Gate writes on appliance identification (target address must not be broadcast)
//
// NOT responsible for:
//   - ERD discovery or polling
//   - Subscription management
//   - ERD value publishing
//   - Bridge startup or lifecycle management
//
// Dependencies:
//   - i_tiny_gea3_erd_client.h, i_mqtt_client.h, tiny_hsm.h, tiny_event.h
// =============================================================================

#ifndef erd_write_bridge_h
#define erd_write_bridge_h

#include "erd_cache.h"
#include "i_mqtt_client.h"
#include "i_tiny_gea3_erd_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "tiny_event.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"

typedef struct {
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  erd_cache_t* erd_cache;
  uint8_t erd_host_address;
  tiny_hsm_t hsm;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  // Pending write state (one write at a time)
  tiny_gea3_erd_client_request_id_t pending_request_id;
  tiny_erd_t pending_erd;
  /* Board the in-flight write was routed to, in sentinel form: 0xFF
   * (PROBE_ENTRY_DEFAULT_ADDRESS) = primary/detected host (unprefixed
   * result topic), otherwise the explicit secondary-board address. */
  uint8_t pending_board_address;
} erd_write_bridge_t;

/*!
 * Initialize the ERD write bridge.
 */
void erd_write_bridge_init(
  erd_write_bridge_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint8_t host_address,
  erd_cache_t* erd_cache);

/*!
 * Destroy the ERD write bridge.
 */
void erd_write_bridge_destroy(
  erd_write_bridge_t* self);

/*!
 * Update the host address after the appliance is identified or re-identified.
 */
void erd_write_bridge_set_host_address(
  erd_write_bridge_t* self,
  uint8_t host_address);

#ifdef __cplusplus
}
#endif

#endif
