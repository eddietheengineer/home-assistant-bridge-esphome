
#pragma once
#include <cstdint>

#include "tiny_erd.h"
#include "erd_lists.h"
#include "bridge_mode.h"
#include "erd_bridge_common.h"

namespace esphome {
namespace geappliances_bridge {

#define ERD_POLL_LIST_MAX_SIZE POLLING_LIST_MAX_SIZE

struct ErdPollListConfig {
  BridgeMode mode;

  bool subscription_active;

  bool appliance_api_parsing;

  // The valid ERD set produced by the feature bit manager. Raw pointer into
  // the manager's fixed array; NULL if not available.
  const tiny_erd_t* feature_bit_valid_erds;
  uint16_t feature_bit_valid_erds_count;
  // User-configured custom ERDs with optional per-ERD board addresses.
  // board_address == PROBE_ENTRY_DEFAULT_ADDRESS means "use primary host address".
  const probe_entry_t* custom_erds;
  uint16_t custom_erds_count;

  uint8_t appliance_type;
};

struct ErdPollListResult {
  probe_entry_t erds[ERD_POLL_LIST_MAX_SIZE];
  uint16_t erds_count;

  const char* description;
};

// Decision logic for build_erd_poll_list():
//   SUBSCRIBE mode (subscription confirmed):
//     -> custom_erds only
//   POLL mode, appliance_api_parsing == true:
//     -> feature_bit_valid_erds + custom_erds
//   POLL mode, appliance_api_parsing == false:
//     -> commonErds + energyErds + applianceApiFeatureErds
//        + appliance-specific ERDs (by appliance_type) + custom_erds
//   AUTO mode: subscription active -> custom_erds only;
//              subscription not active -> same as POLL mode.
ErdPollListResult build_erd_poll_list(const ErdPollListConfig& config);

}  // namespace geappliances_bridge
}  // namespace esphome
