/*!
 * @file
 * @brief ErdPollListBuilder implementation.
 */

#include "erd_poll_list_builder.h"
#include "erd_lists.h"
#include "geappliances_bridge_log.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

GEA_TAG(TAG) = "erd_poll_list_builder";

namespace esphome {
namespace geappliances_bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint16_t append_erds(uint16_t* out, uint16_t count, const uint16_t* list, uint16_t list_count)
{
  uint16_t n = (list_count > (ERD_POLL_LIST_MAX_SIZE - count)) ? (ERD_POLL_LIST_MAX_SIZE - count) : list_count;
  if (n < list_count) {
    ESP_LOGW(TAG, "Poll list capacity exceeded; truncated %u ERDs (dropped %u)", n, list_count - n);
  }
  if (list && n > 0) {
    std::memcpy(out + count, list, n * sizeof(uint16_t));
  }
  return count + n;
}

static void deduplicate(uint16_t* erds, uint16_t& count)
{
  if (count <= 1) return;
  std::sort(erds, erds + count);
  uint16_t write = 1;
  for (uint16_t read = 1; read < count; read++) {
    if (erds[read] != erds[write - 1]) {
      erds[write++] = erds[read];
    }
  }
  count = write;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config)
{
  ErdPollListResult result;
  std::memset(result.erds, 0, sizeof(result.erds));
  result.erds_count = 0;
  result.description = "";

  bool is_subscribe_mode = (config.mode == BRIDGE_MODE_SUBSCRIBE) ||
                           (config.mode == BRIDGE_MODE_AUTO && config.subscription_active);

  // -----------------------------------------------------------------------
  // SUBSCRIBE mode (subscription confirmed): only custom ERDs
  // -----------------------------------------------------------------------
  if (is_subscribe_mode) {
    result.erds_count = append_erds(result.erds, 0, config.custom_erds, config.custom_erds_count);
    result.description = "subscription mode: custom ERDs only";
    return result;
  }

  // -----------------------------------------------------------------------
  // POLL mode (or AUTO fallback): decide based on appliance_api_parsing
  // -----------------------------------------------------------------------
  if (config.appliance_api_parsing) {
    // Use feature-bit-validated ERDs + custom ERDs.
    result.erds_count = append_erds(result.erds, 0, config.feature_bit_valid_erds, config.feature_bit_valid_erds_count);
    result.erds_count = append_erds(result.erds, result.erds_count, config.custom_erds, config.custom_erds_count);
    result.description = "poll mode with API parsing: feature-bit ERDs + custom ERDs";
  } else {
    // Full discovery: common + energy + appliance API feature + appliance-specific + custom.
    result.erds_count = append_erds(result.erds, 0, commonErds, commonErdCount);
    result.erds_count = append_erds(result.erds, result.erds_count, energyErds, energyErdCount);
    result.erds_count = append_erds(result.erds, result.erds_count, applianceApiFeatureErds, applianceApiFeatureErdCount);

    // Appliance-specific ERDs (skip if type is out of range).
    if (config.appliance_type < maximumApplianceType) {
      const auto& group = applianceTypeToErdGroupTranslation[config.appliance_type];
      result.erds_count = append_erds(result.erds, result.erds_count, group.erdList, group.erdCount);
    }

    result.erds_count = append_erds(result.erds, result.erds_count, config.custom_erds, config.custom_erds_count);
    result.description = "poll mode without API parsing: full ERD list";
  }

  // Deduplicate in place.
  deduplicate(result.erds, result.erds_count);

  return result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
