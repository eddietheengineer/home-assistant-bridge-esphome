/*!
 * @file
 * @brief ErdPollListBuilder implementation.
 */

#include "erd_poll_list_builder.h"
#include "erd_lists.h"
#include <set>

namespace esphome {
namespace geappliances_bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void append_erds(std::vector<uint16_t>& out, const uint16_t* list, uint16_t count)
{
  for (uint16_t i = 0; i < count; i++) {
    out.push_back(list[i]);
  }
}

static void deduplicate(std::vector<uint16_t>& erds)
{
  std::set<uint16_t> seen;
  for (auto it = erds.begin(); it != erds.end(); ) {
    if (seen.count(*it)) {
      it = erds.erase(it);
    } else {
      seen.insert(*it);
      ++it;
    }
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config)
{
  ErdPollListResult result;

  bool is_subscribe_mode = (config.mode == BRIDGE_MODE_SUBSCRIBE) ||
                           (config.mode == BRIDGE_MODE_AUTO && config.subscription_active);

  // -----------------------------------------------------------------------
  // SUBSCRIBE mode (subscription confirmed): only custom ERDs
  // -----------------------------------------------------------------------
  if (is_subscribe_mode) {
    append_erds(result.erds, config.custom_erds ? config.custom_erds->data() : nullptr,
                config.custom_erds ? static_cast<uint16_t>(config.custom_erds->size()) : 0);
    result.description = "subscription mode: custom ERDs only";
    return result;
  }

  // -----------------------------------------------------------------------
  // POLL mode (or AUTO fallback): decide based on appliance_api_parsing
  // -----------------------------------------------------------------------
  if (config.appliance_api_parsing) {
    // Use feature-bit-validated ERDs + custom ERDs.
    append_erds(result.erds, config.feature_bit_valid_erds, config.feature_bit_valid_erds_count);
    append_erds(result.erds, config.custom_erds ? config.custom_erds->data() : nullptr,
                config.custom_erds ? static_cast<uint16_t>(config.custom_erds->size()) : 0);
    result.description = "poll mode with API parsing: feature-bit ERDs + custom ERDs";
  } else {
    // Full discovery: common + energy + appliance API feature + appliance-specific + custom.
    append_erds(result.erds, commonErds, commonErdCount);
    append_erds(result.erds, energyErds, energyErdCount);
    append_erds(result.erds, applianceApiFeatureErds, applianceApiFeatureErdCount);

    // Appliance-specific ERDs (skip if type is out of range).
    if (config.appliance_type < maximumApplianceType) {
      const auto& group = applianceTypeToErdGroupTranslation[config.appliance_type];
      append_erds(result.erds, group.erdList, group.erdCount);
    }

    append_erds(result.erds, config.custom_erds ? config.custom_erds->data() : nullptr,
                config.custom_erds ? static_cast<uint16_t>(config.custom_erds->size()) : 0);
    result.description = "poll mode without API parsing: full ERD list";
  }

  // Deduplicate in place.
  deduplicate(result.erds);

  return result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
