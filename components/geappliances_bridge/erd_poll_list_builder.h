/*!
 * @file
 * @brief Builds the ERD poll list based on bridge mode and configuration.
 *
 * Pure function — no HSM, no timers, no I/O.  Given the current operating mode,
 * subscription state, feature-bit results, custom ERDs, and appliance type,
 * returns a deduplicated list of ERDs that the polling bridge should probe.
 *
 * Decision logic:
 *
 *   SUBSCRIBE mode (subscription confirmed):
 *     → custom_erds only
 *
 *   POLL mode, appliance_api_parsing == true:
 *     → feature_bit_valid_erds + custom_erds
 *
 *   POLL mode, appliance_api_parsing == false:
 *     → commonErds + energyErds + applianceApiFeatureErds
 *       + appliance-specific ERDs (by appliance_type)
 *       + custom_erds
 *
 *   AUTO mode, subscription active:
 *     → custom_erds only (same as SUBSCRIBE)
 *
 *   AUTO mode, subscription not active (fallback):
 *     → same as POLL mode with current appliance_api_parsing setting
 *
 * The returned list is deduplicated.  Order is: standard ERDs first
 * (in their original group order), then custom ERDs.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bridge_mode.h"

namespace esphome {
namespace geappliances_bridge {

/*
 * Configuration for building the ERD poll list.
 */
struct ErdPollListConfig {
  /// The operating mode (POLL, SUBSCRIBE, or AUTO).
  BridgeMode mode;

  /// Whether the appliance supports GEA3 subscriptions.
  /// For GEA2 appliances this is always false.
  bool subscription_capable;

  /// Whether subscription is currently active and confirmed.
  /// Only relevant when mode == SUBSCRIBE or (mode == AUTO and subscription is valid).
  bool subscription_active;

  /// Whether appliance API feature bit filtering is enabled.
  /// When true, only ERDs reported by the feature bits are polled.
  bool appliance_api_parsing;

  /// The valid ERD set produced by the feature bit manager.
  /// Empty if feature bits are not available or parsing is disabled.
  const std::vector<uint16_t>* feature_bit_valid_erds;

  /// User-configured custom ERDs to always poll.
  const std::vector<uint16_t>* custom_erds;

  /// The discovered appliance type (0-255).
  /// Used to look up appliance-specific ERDs from erd_lists.h.
  uint8_t appliance_type;
};

/*
 * The result of building the poll list.
 */
struct ErdPollListResult {
  /// The list of ERDs to probe.
  std::vector<uint16_t> erds;

  /// Human-readable description of how the list was built (for logging).
  std::string description;
};

/*
 * Build the list of ERDs the polling bridge should probe.
 */
ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config);

}  // namespace geappliances_bridge
}  // namespace esphome
