/*!
 * @file
 * @brief Polls ERDs and publishes to MQTT server (polling mode)
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Periodically poll a list of ERDs from the appliance and publish their
//       values to MQTT; fulfill write commands received from MQTT.
//
// Responsibilities:
//   - Maintain and iterate a dynamic polling list
//   - Drive a tiny_hsm for ERD discovery, polling, and appliance-lost recovery
//   - Accept optional pre-populated lists (api_parsed_list, custom_erd_list)
//     to skip or augment runtime ERD discovery
//   - Report polling health metrics (cycle count, last cycle time)
//
// NOT responsible for:
//   - Subscription-mode operation (see mqtt_bridge.h)
//   - Deciding which ERDs are valid (filtered upstream by i_mqtt_client)
//   - Bridge initialization or startup phase management
//
// ---- 3-Phase Polling Lifecycle ----
//
// Phase 1 — Build Verification List (no reads)
//   Determines which ERDs to probe in Phase 2. Contents depend on config:
//   - POLL or AUTO→poll, appliance_api_parsing=false:
//       commonErds + energyErds + applianceApiFeatureErds +
//       appliance-specific (erd_lists.h) + custom_erds
//   - POLL or AUTO→poll, appliance_api_parsing=true:
//       feature_bit_manager valid ERD list + custom_erds
//   - SUBSCRIBE or AUTO→subscribing:
//       custom_erds only (empty if none configured)
//
// Phase 2 — Verification (sequential reads; HSM states before state_polling)
//   Each ERD in the verification list is read once:
//   - read_completed             → register ERD + publish value → next ERD
//   - read_failed(not_supported) → permanently exclude          → next ERD
//   - read_failed(retries_exhausted) → skip, not added to polling list → next ERD
//   Result: erd_polling_list contains only ERDs that responded successfully.
//
// Phase 3 — Steady-State Polling (state_polling)
//   All registered ERDs are read sequentially each cycle. Timer semantics:
//   - Timer fires mid-cycle (cycle not yet complete):
//       set restart_pending=true; let the cycle finish naturally.
//   - Timer fires after cycle already complete:
//       start next cycle immediately; re-arm timer.
//   - Cycle completes with restart_pending=true:
//       start next cycle immediately; re-arm timer; clear restart_pending.
//   - Cycle completes, timer still armed:
//       wait for timer to fire.
//   - Cycle completes, timer not armed, no restart_pending:
//       start next cycle immediately; re-arm timer.
//
// Dependencies:
//   - i_mqtt_client.h, i_tiny_gea3_erd_client.h, tiny_hsm.h, tiny_timer.h
//   - erd_lists.h (appliance ERD list arrays)
// =============================================================================

#ifndef mqtt_bridge_polling_h
#define mqtt_bridge_polling_h

#include "i_mqtt_client.h"
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
#include "erd_lists.h"

typedef struct {
  tiny_erd_t* erd_polling_list;       // Heap-allocated, dynamically sized
  uint16_t polling_list_count;
  uint16_t polling_list_capacity;      // Allocated capacity of erd_polling_list
  uint32_t polling_interval_ms;
  tiny_timer_group_t* timer_group;
  i_tiny_gea3_erd_client_t* erd_client;
  i_mqtt_client_t* mqtt_client;
  tiny_timer_t timer;
  tiny_timer_t appliance_lost_timer;
  tiny_timer_t polling_timer;
  tiny_event_subscription_t mqtt_write_request_subscription;
  tiny_event_subscription_t mqtt_disconnect_subscription;
  tiny_event_subscription_t erd_client_activity_subscription;
  tiny_hsm_t hsm;
  tiny_hsm_state_t next_discovery_state;
  void* erd_set;
  void* erd_cache;
  // Set of ERDs that have been added to the polling list but not yet
  // registered on MQTT (added via add_erd_to_polling_list_no_register).
  // On first successful read, these are registered and removed from this set.
  void* pending_registration_set;
  tiny_gea3_erd_client_request_id_t request_id;
  uint8_t erd_host_address;
  uint8_t appliance_type;
  const tiny_erd_t* appliance_erd_list;
  uint16_t appliance_erd_list_count;
  uint16_t erd_index;
  // Number of ERDs in the current polling cycle that have completed (success
  // or failure).  Used together with erd_index to determine when a full cycle
  // has finished — the cycle only restarts when cycle_completed_count equals
  // polling_list_count AND the polling timer has expired.
  uint16_t cycle_completed_count;
  bool only_publish_on_change;
  // Set to true once the HSM transitions into state_polling (all ERD
  // discovery phases have completed). Reset to false on appliance loss/
  // reconnect. Used externally to gate HA discovery until polling is steady.
  bool polling_list_complete;
  // Updated at each state entry with a human-readable name of the current HSM
  // state. Initialized to nullptr; callers may watch this for changes to emit
  // debug log messages without coupling mqtt_bridge.cpp to ESP logging headers.
  const char* current_state_name;
  // Optional pre-populated polling list from appliance API parsing.
  // When non-NULL, discovery states are skipped and this list is polled directly.
  const tiny_erd_t* api_parsed_list;
  uint16_t api_parsed_list_count;
  // Optional list of user-configured custom ERDs to poll in addition to the
  // standard list. Set after mqtt_bridge_polling_init(). Works with both
  // discovery mode and api_parsed_list mode.
  const tiny_erd_t* custom_erd_list;
  uint16_t custom_erd_list_count;
  // When mqtt_bridge_polling_init_at_address() is used this stores the
  // pre-known appliance address so that the bridge never broadcasts to 0xFF
  // on re-identification (e.g. after appliance_lost_timer fires).  Zero means
  // "no pre-known address — use broadcast discovery" (the default from
  // mqtt_bridge_polling_init()).
  uint8_t known_host_address;
  // Health metrics: updated by the polling bridge as cycles complete.
  // cycle_start_ms: millis() when the current cycle's first read was sent.
  // last_cycle_time_ms: duration of the last completed cycle (ms).
  // cycle_count: total completed cycles since init.
  uint32_t cycle_start_ms;
  uint32_t last_cycle_time_ms;
  uint32_t cycle_count;
  // True while the polling timer is armed (between arm_polling_timer and the
  // next signal_polling_timer_expired).  Used to decide whether to restart
  // polling immediately when a cycle finishes: if the timer is armed, wait for
  // it to fire; if not, start the next cycle right away.
  bool polling_timer_armed;
  // Set to true when the polling_interval timer fires while a cycle is still
  // in progress (cycle_completed_count < polling_list_count).  The in-progress
  // cycle is allowed to finish, then the cycle-completion handler restarts
  // immediately instead of waiting for another timer interval.
  bool restart_pending;
} mqtt_bridge_polling_t;

/*!
 * Initialize the MQTT polling bridge.
 */
void mqtt_bridge_polling_init(
  mqtt_bridge_polling_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms,
  bool only_publish_on_change);

/*!
 * Initialize the MQTT polling bridge with a pre-known host address.
 *
 * Unlike mqtt_bridge_polling_init(), this variant skips the broadcast
 * identification step (reading ERD 0x0008 from 0xFF) because the appliance
 * address is already known.  If api_list is non-NULL the bridge goes directly
 * to state_polling; otherwise it runs the full ERD discovery chain starting at
 * state_add_common_erds.  Use this when starting a secondary (custom-ERD-only)
 * polling bridge alongside a subscription bridge that has already identified
 * the appliance.
 */
void mqtt_bridge_polling_init_at_address(
  mqtt_bridge_polling_t* self,
  tiny_timer_group_t* timer_group,
  i_tiny_gea3_erd_client_t* erd_client,
  i_mqtt_client_t* mqtt_client,
  uint32_t polling_interval_ms,
  bool only_publish_on_change,
  uint8_t known_host_address,
  const tiny_erd_t* api_list,
  uint16_t api_list_count);

/*!
 * Destroy the MQTT polling bridge.
 */
void mqtt_bridge_polling_destroy(
  mqtt_bridge_polling_t* self);

#endif
