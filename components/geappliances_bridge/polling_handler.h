// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Handle appliance-side ERD polling via the GEA3 ERD client.
//       Writes ERD updates to ErdStateTable. Does NOT call the MQTT client.
//
// Responsibilities:
//   - Poll a configured set of ERDs on an interval using the GEA3 ERD client
//   - When poll responses arrive, write values to ErdStateTable
//   - Enforce only_publish_on_change semantics:
//     - true  → update_erd_value() only (flag set only on change)
//     - false → update_erd_value() + set_publish_flag() (unconditional)
//   - Handle polling errors and retry logic
//
// NOT responsible for:
//   - Any MQTT publishing
//   - Timer management (timer is started/stopped by ApplianceSideStateMachine)
//
// Dependencies:
//   - erd_state_table.h (writes values here)
//   - i_tiny_gea3_erd_client.h (reads via this)
//   - tiny_timer.h (polling timer)
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_erd.h"
#include "tiny_timer.h"
#include "i_tiny_event.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
}

#include "erd_state_table.h"

namespace esphome {
namespace geappliances_bridge {

class PollingHandler {
 public:
  PollingHandler(i_tiny_gea3_erd_client_t* erd_client,
                 ErdStateTable* state_table,
                 uint32_t polling_interval_ms,
                 bool only_publish_on_change);

  ~PollingHandler();

  /// Add an ERD to the polling list.
  void add_erd_to_poll(tiny_erd_t erd_id);

  /// Remove an ERD from the polling list.
  void remove_erd_from_poll(tiny_erd_t erd_id);

  /// Change the polling interval (in milliseconds).
  void set_polling_interval(uint32_t ms);

  /// Trigger an immediate poll cycle (e.g., on reconnect).
  void poll_now(uint8_t address);

  /// Called by the GEA3 client callback when a read (poll) response arrives.
  /// Writes the value to the ErdStateTable with publish flag semantics.
  void handle_poll_response(tiny_erd_t erd_id, const uint8_t* value, uint8_t size);

  /// Called by the GEA3 client callback when a read (poll) fails.
  void handle_poll_failure(tiny_erd_t erd_id);

  /// Start the periodic poll timer.
  void start(tiny_timer_group_t* timer_group, uint8_t address);

  /// Stop the periodic poll timer.
  void stop(tiny_timer_group_t* timer_group);

  /// Returns true if the polling timer is running.
  bool is_running() const;

  /// Returns the current polling interval in milliseconds.
  uint32_t get_polling_interval() const;

  /// Returns the only_publish_on_change setting.
  bool get_only_publish_on_change() const;

  /// Returns the current polling ERD list.
  const std::vector<tiny_erd_t>& get_polling_erds() const;

  /// Returns true if at least one full polling cycle has completed.
  /// Used by HA discovery to gate publishing until ERDs have been read.
  bool has_completed_first_cycle() const;

 private:
  static void on_poll_timer_(void* context);
  static void on_erd_client_activity_(void* context, const void* args);

  i_tiny_gea3_erd_client_t* erd_client_;
  ErdStateTable* state_table_;
  uint32_t polling_interval_ms_;
  bool only_publish_on_change_;
  std::vector<tiny_erd_t> polling_erds_;

  tiny_timer_group_t* timer_group_;
  uint8_t address_;
  tiny_timer_t poll_timer_;
  tiny_event_subscription_t erd_client_activity_sub_;
  bool timer_running_;
  bool has_completed_first_cycle_;
  uint8_t pending_reads_;  // tracks in-flight reads for cycle completion detection
};

}  // namespace geappliances_bridge
}  // namespace esphome
