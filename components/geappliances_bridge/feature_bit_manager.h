/*!
 * @file
 * @brief Feature bit manager for the GEA bridge.
 *
 * Fully self-driving: owns its own timers and event subscriptions.
 * Reads and parses appliance API feature bit ERDs (0x0092-0x010D),
 * building a filtered ERD list for polling mode.
 *
 * The bridge calls init() to configure, then start() to begin reading.
 * The manager subscribes to ERD client activity events and drives
 * the read sequence autonomously.  When all ERDs have been read,
 * it uses a periodic timer to perform incremental parsing (one step
 * per tick) to avoid triggering the ESP32 Task Watchdog Timer.
 * The bridge polls get_state() to check progress.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Read appliance API feature bit ERDs and produce the set of ERDs that
//       are available on this appliance.
//
// Responsibilities:
//   - Read the 11 feature bit ERDs (0x0092-0x0097, 0x0109-0x010D) in sequence
//   - Own event subscription to ERD client activity (self-driving)
//   - Incrementally parse bitmasks into ERD sets across multiple timer ticks
//     to avoid triggering the ESP32 Task Watchdog Timer
//   - Expose the resulting valid ERD set and string ERD set via getters
//
// NOT responsible for:
//   - Using the ERD set (callers decide how to apply it)
//   - Managing polling or subscription bridges
//   - Filtering ERDs during MQTT publishing (ErdRegistry / MqttClientAdapter)
//
// Dependencies:
//   - i_tiny_gea3_erd_client
//   - tiny_event, tiny_event_subscription, tiny_timer
//   - appliance_api_feature_lists.h (compile-time bitmask descriptors)
// =============================================================================

#ifndef FEATURE_BIT_MANAGER_H
#define FEATURE_BIT_MANAGER_H

#include <cstdint>
#include <string>

extern "C" {
#include "i_tiny_gea3_erd_client.h"
#include "tiny_event.h"
#include "tiny_event_subscription.h"
#include "tiny_timer.h"
}

namespace esphome {
namespace geappliances_bridge {

enum FeatureBitState {
  FEATURE_BIT_STATE_READING_0092,
  FEATURE_BIT_STATE_READING_0093,
  FEATURE_BIT_STATE_READING_0094,
  FEATURE_BIT_STATE_READING_0095,
  FEATURE_BIT_STATE_READING_0096,
  FEATURE_BIT_STATE_READING_0097,
  FEATURE_BIT_STATE_READING_0109,
  FEATURE_BIT_STATE_READING_010A,
  FEATURE_BIT_STATE_READING_010B,
  FEATURE_BIT_STATE_READING_010C,
  FEATURE_BIT_STATE_READING_010D,
  FEATURE_BIT_STATE_PARSING,
  FEATURE_BIT_STATE_FAILED,
  FEATURE_BIT_STATE_COMPLETE,
};

/* Maximum number of ERDs the feature bit manager can track.
 * This covers all possible ERDs from common + appliance feature descriptors. */
#define FEATURE_BIT_MAX_ERDS 645

/*!
 * Manages reading and parsing of appliance API feature bit ERDs.
 */
class FeatureBitManager {
 public:
  /* How many common feature descriptors to process per parse tick.
   * 17 total descriptors; processing 4 per tick spreads the work
   * across ~5 timer callbacks instead of doing all at once, which
   * avoids triggering the Task Watchdog Timer on ESP32-C3. */
  static constexpr uint16_t COMMON_PARSE_PER_CALL = 4;

  /* Timer interval for incremental parsing (milliseconds). */
  static constexpr uint32_t PARSE_TICK_MS = 5;

  /* Delay before retrying a failed queue operation (milliseconds). */
  static constexpr uint32_t QUEUE_RETRY_MS = 50;

  void init(i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address,
            tiny_timer_group_t* timer_group);

  /// Unsubscribe from events and stop timers. Safe to call multiple times.
  void cleanup();

  /// Start the feature-bit reading sequence.  Idempotent if already past the first state.
  void start();

  /// Returns the number of valid ERDs.
  uint16_t get_valid_erd_count() const;

  /// Returns the valid ERD at the given index (0-based).
  tiny_erd_t get_valid_erd(uint16_t idx) const;

  FeatureBitState get_state() const { return state_; }

 private:
  /// Called from the ERD client activity subscription callback.
  void on_erd_activity_(const void* args);

  /// Handle a successful ERD read (store data, advance state).
  void handle_read_completed_(tiny_erd_t erd, const void* data, uint8_t size);

  /// Timer callback for incremental parsing (static for tiny_timer API).
  static void parse_timer_callback_(void* context);

  /// Drive one step of incremental parsing.
  void parse_next_step_();

  /// Start the periodic parse timer (called when transitioning to PARSING).
  void start_parse_timer_();

  /// Map a READING state to the ERD value and queue the read.
  void queue_erd_read_();

  /// Return the ERD this READING state is waiting for (for event filtering).
  tiny_erd_t get_expected_erd_() const;

  /// Advance state to the next ERD in the sequence (on failure or skip).
  void skip_to_next_erd_(tiny_erd_t failed_erd);

  /// One-shot retry timer callback (static for tiny_timer API).
  static void queue_retry_timer_callback_(void* context);

  /// Retry queue_erd_read_ after a queue-full delay.
  void queue_retry_();

  /// Add an ERD to the valid list (deduplicated).
  void add_valid_erd_(tiny_erd_t erd);

  FeatureBitState state_{FEATURE_BIT_STATE_READING_0092};
  bool read_queued_{false};  /* true while a read is in-flight (guards idempotent start/queue) */

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
  tiny_timer_group_t* timer_group_{nullptr};

  /* Event subscription for ERD client activity */
  tiny_event_subscription_t erd_activity_subscription_;

  /* Timer for incremental parsing */
  tiny_timer_t parse_timer_;

  /* One-shot retry timer for queue-full scenario */
  tiny_timer_t queue_retry_timer_;

  struct FeatureBitErdData {
    uint8_t erd_0092[8]{};
    uint8_t erd_0093[8]{};
    uint8_t erd_0094[8]{};
    uint8_t erd_0095[8]{};
    uint8_t erd_0096[8]{};
    uint8_t erd_0097[8]{};
    uint8_t erd_0109[8]{};
    uint8_t erd_010A[8]{};
    uint8_t erd_010B[8]{};
    uint8_t erd_010C[8]{};
    uint8_t erd_010D[8]{};
    uint8_t erd_0092_size{0};
    uint8_t erd_0093_size{0};
    uint8_t erd_0094_size{0};
    uint8_t erd_0095_size{0};
    uint8_t erd_0096_size{0};
    uint8_t erd_0097_size{0};
    uint8_t erd_0109_size{0};
    uint8_t erd_010A_size{0};
    uint8_t erd_010B_size{0};
    uint8_t erd_010C_size{0};
    uint8_t erd_010D_size{0};
  };

  FeatureBitErdData erd_data_;
  /* Fixed-capacity ERD list — replaces std::set and std::vector. */
public:
  tiny_erd_t valid_erds_[FEATURE_BIT_MAX_ERDS];
private:
  uint16_t valid_erds_count_{0};
  bool valid_list_ready_{false};

  uint8_t parse_erd_idx_{0};  /* which appliance ERD we're parsing next (0-9) */
  uint16_t common_parse_idx_{0};  /* which common feature descriptor we're parsing next (0-17) */
  bool parse_common_done_{false};  /* whether ERD 0x0092 common features are parsed */
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // FEATURE_BIT_MANAGER_H
