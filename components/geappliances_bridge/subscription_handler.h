// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Handle appliance-side subscription logic. Subscribes to ERD publications
//       from the appliance and writes updates to ErdStateTable. Does NOT call
//       the MQTT client directly.
//
// Responsibilities:
//   - Subscribe to all ERDs at the appliance's address via GEA3 ERD client
//   - On subscription publications, write values to ErdStateTable
//   - Handle subscription lifecycle (subscribe, retain, re-subscribe on failure)
//   - Track which ERDs have been seen (for registration in Phase 3)
//
// NOT responsible for:
//   - Publishing to MQTT (MqttSideStateMachine does that in Phase 3)
//   - Write commands (WriteHandler does that)
//   - Bridge initialization or startup phase management
//
// Dependencies:
//   - i_tiny_gea3_erd_client.h (ERD client interface)
//   - erd_state_table.h (runtime value storage)
//   - tiny_hsm.h, tiny_timer.h (state machine + timers)
// =============================================================================

#pragma once

#include <cstdint>
#include <set>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_hsm.h"
#include "tiny_timer.h"
#include "i_tiny_event.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
}

#include "erd_state_table.h"

namespace esphome {
namespace geappliances_bridge {

// Timing constants (from existing mqtt_bridge_common.h)
static constexpr uint32_t SUB_RESUBSCRIBE_DELAY_MS = 1000;
static constexpr uint32_t SUB_RETENTION_PERIOD_MS = 30 * 1000;

class SubscriptionHandler {
 public:
  SubscriptionHandler(i_tiny_gea3_erd_client_t* erd_client,
                      ErdStateTable* state_table,
                      tiny_timer_group_t* timer_group);

  ~SubscriptionHandler();

  // Initialize and start subscribing at the given appliance address.
  // The handler listens for subscription publications from the appliance
  // and writes them to ErdStateTable. No initial reads are performed —
  // the appliance sends a burst of subscription publications that naturally
  // populate ErdStateTable.
  void start(uint8_t address);

  // Stop and clean up.
  void stop();

  // Called from the main loop each cycle (non-blocking).
  void loop();

  // Check if an ERD has been seen via subscription.
  bool is_known_erd(tiny_erd_t erd_id) const;

  // Get the set of ERDs seen so far.
  const std::set<tiny_erd_t>& get_known_erds() const { return known_erds_; }

 private:
  // HSM states (friend functions so they can access private members)
  friend tiny_hsm_result_t sub_state_top(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t state_subscribing(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t state_subscribed(tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

  // ERD client activity callback
  static void on_erd_client_activity_(void* context, const void* args);
  static void on_resubscribe_timer_(void* context);
  static void on_retention_timer_(void* context);

  i_tiny_gea3_erd_client_t* erd_client_;
  ErdStateTable* state_table_;
  tiny_timer_group_t* timer_group_;
  uint8_t address_;

  tiny_timer_t timer_;
  tiny_event_subscription_t erd_client_activity_sub_;
  tiny_hsm_t hsm_;

  std::set<tiny_erd_t> known_erds_;

  // HSM signal IDs (defined at namespace scope so friend functions can see them)
  static const tiny_hsm_signal_t signal_subscription_added_or_retained;
  static const tiny_hsm_signal_t signal_subscription_failed;
  static const tiny_hsm_signal_t signal_subscription_publication_received;
  static const tiny_hsm_signal_t signal_subscription_host_came_online;
};

}  // namespace geappliances_bridge
}  // namespace esphome
