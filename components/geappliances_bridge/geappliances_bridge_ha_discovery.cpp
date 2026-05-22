/*\!
 * @file
 * @brief Home Assistant device-discovery publishing.
 *
 * Delegates to HaDiscoveryManager for all HA discovery logic.
 * This file retains only the bridge-facing delegation methods and
 * the sync function that keeps legacy members in step with the manager.
 */

#include "geappliances_bridge.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
static const char* const TAG = "geappliances_bridge";
#ifdef __clang__
#pragma clang diagnostic pop
#endif

// ---------------------------------------------------------------------------
// on_ha_discovery_erd_seen_() — called when a new ERD is seen via subscription
// ---------------------------------------------------------------------------

void GeappliancesBridge::on_ha_discovery_erd_seen_(tiny_erd_t erd)
{
  this->ha_discovery_manager_.on_erd_seen(erd);
}

// ---------------------------------------------------------------------------
// Sync legacy members from HaDiscoveryManager for backward compatibility
// ---------------------------------------------------------------------------

void GeappliancesBridge::sync_ha_discovery_legacy_members_()
{
  HaDiscoveryState state = this->ha_discovery_manager_.get_state();

  if (state == HA_DISCOVERY_IDLE) {
    this->ha_discovery_pending_             = false;
    this->ha_discovery_published_           = false;
    this->ha_discovery_publish_in_progress_ = false;
  } else if (state == HA_DISCOVERY_WAITING_FOR_READY) {
    this->ha_discovery_pending_             = true;
    this->ha_discovery_published_           = false;
    this->ha_discovery_publish_in_progress_ = false;
  } else if (state == HA_DISCOVERY_DOWNLOADING) {
    this->ha_discovery_pending_             = false;
    this->ha_discovery_published_           = false;
    this->ha_discovery_publish_in_progress_ = true;
  } else if (state == HA_DISCOVERY_PUBLISHING) {
    this->ha_discovery_pending_             = false;
    this->ha_discovery_published_           = false;
    this->ha_discovery_publish_in_progress_ = true;
  } else if (state == HA_DISCOVERY_COMPLETE) {
    this->ha_discovery_pending_             = false;
    this->ha_discovery_published_           = true;
    this->ha_discovery_publish_in_progress_ = false;
  } else if (state == HA_DISCOVERY_FAILED) {
    this->ha_discovery_pending_             = false;
    this->ha_discovery_published_           = true;
    this->ha_discovery_publish_in_progress_ = false;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
