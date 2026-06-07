// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Coordinate the three handlers (SubscriptionHandler, PollingHandler,
//       WriteHandler) based on configuration and appliance discovery state.
//       Transitions to Running when the appliance address is discovered.
//
// States:
//   - Idle:   waiting for appliance discovery (appliance_address == 0)
//   - Running: actively delegating to handlers
//   - Error:  handler failure; will retry after delay
//
// NOT responsible for:
//   - Any MQTT publishing
//   - The 200 ms GEA2 busy-loop (handled by GeappliancesBridge)
//
// Dependencies:
//   - erd_state_table.h
//   - global_state_registry.h
//   - write_queue.h
//   - subscription_handler.h
//   - polling_handler.h
//   - write_handler.h
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_erd.h"
#include "tiny_timer.h"
}

#include "erd_state_table.h"
#include "global_state_registry.h"
#include "write_queue.h"
#include "subscription_handler.h"
#include "polling_handler.h"
#include "write_handler.h"

namespace esphome {
namespace geappliances_bridge {

static constexpr uint32_t ERROR_RETRY_DELAY_MS = 5000;

struct ApplianceSideConfig {
  bool enable_subscriptions;
  bool enable_polling;
  uint32_t polling_interval_ms;
  bool only_publish_on_change;
  std::vector<tiny_erd_t> subscription_erds;
  std::vector<tiny_erd_t> polling_erds;
};

enum class ApplianceSideState {
  IDLE,
  RUNNING,
  ERROR
};

class ApplianceSideStateMachine {
 public:
  ApplianceSideStateMachine(ErdStateTable* state_table,
                            GlobalStateRegistry* registry,
                            WriteQueue* write_queue,
                            i_tiny_gea3_erd_client_t* erd_client);

  ~ApplianceSideStateMachine();

  /// Set the configuration (called once during initialization).
  void set_config(const ApplianceSideConfig& config);

  /// Set the timer group for polling and error retry timers.
  void set_timer_group(tiny_timer_group_t* timer_group);

  /// Set the handler pointers (owned by the bridge, not the FSM).
  void set_handlers(SubscriptionHandler* sub, PollingHandler* poll, WriteHandler* write);

  /// Fast, non-blocking; called from GeappliancesBridge::loop().
  /// Must not block — the 200 ms GEA2 busy-loop is handled by
  /// GeappliancesBridge::run_protocol_stack_() before this is called.
  void loop();

  /// Returns the current state.
  ApplianceSideState get_current_state() const;

  /// Returns the subscription handler for external access.
  SubscriptionHandler* get_subscription_handler();

  /// Returns the polling handler for external access.
  PollingHandler* get_polling_handler();

  /// Returns the write handler for external access.
  WriteHandler* get_write_handler();

  // Test hooks — not part of the public API
  void transition_to_error_for_test();
  void on_error_retry_timer_for_test();

 private:
  void on_appliance_address_changed(void* context, const void* args);
  void on_error_retry_timer(void* context);
  void transition_to(ApplianceSideState next);
  void start_handlers();
  void stop_handlers();

  ErdStateTable* state_table_;
  GlobalStateRegistry* registry_;
  WriteQueue* write_queue_;
  i_tiny_gea3_erd_client_t* erd_client_;

  ApplianceSideState current_state_;
  ApplianceSideConfig config_;
  bool config_set_{false};  // True after set_config() called; guards early transition

  SubscriptionHandler* subscription_handler_;
  PollingHandler* polling_handler_;
  WriteHandler* write_handler_;

  tiny_event_subscription_t appliance_address_sub_;
  tiny_timer_group_t* timer_group_;
  tiny_timer_t error_retry_timer_;
  bool error_retry_timer_running_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
