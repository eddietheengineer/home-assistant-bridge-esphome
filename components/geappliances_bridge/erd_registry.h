// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Be the single authoritative source for which ERDs are valid,
//       which are string-typed, and which are registered at runtime.
//
// Responsibilities:
//   - Own the valid-ERD set (populated by FeatureBitManager at startup)
//   - Own the string-ERD set (populated from generated ha_string_erd_ids[])
//   - Own the registered-ERD set (appended by the MQTT adapter at runtime)
//   - Expose query methods used by the MQTT adapter during publish
//   - Expose read-only accessors used by HaDiscoveryManager and diagnostics
//
// NOT responsible for:
//   - Reading ERDs from the appliance (FeatureBitManager does that)
//   - Publishing MQTT messages (EsphomeMqttClientAdapter does that)
//   - Any bridge lifecycle management
//
// Dependencies:
//   - tiny_erd.h (tiny_erd_t type)
// =============================================================================

#pragma once

#include <set>
#include <cstdint>

extern "C" {
#include "tiny_erd.h"
}

namespace esphome {
namespace geappliances_bridge {

class ErdRegistry {
 public:
  // -------------------------------------------------------------------------
  // Setup methods (called during bridge initialization)
  // -------------------------------------------------------------------------

  /// Populate string-type ERDs from the generated ha_string_erd_ids[] array.
  /// Must be called before the registry is passed to the MQTT adapter.
  void init_string_erds(const uint16_t* ids, uint16_t count);

  /// Set string-type ERDs directly from a set (used in tests and custom paths).
  void set_string_erds(const std::set<tiny_erd_t>& erds);

  /// Copy the valid-ERD set from FeatureBitManager and enable valid-ERD
  /// filtering. An empty set is ignored so filtering stays disabled.
  void set_valid_erds(const std::set<tiny_erd_t>& erds);

  /// Reset the registered-ERD set. Call before bridge (re-)initialization.
  void clear_registered_erds();

  // -------------------------------------------------------------------------
  // Runtime methods (called by the MQTT adapter during operation)
  // -------------------------------------------------------------------------

  /// Record that an ERD has been registered at runtime.
  void register_erd(tiny_erd_t erd);

  // -------------------------------------------------------------------------
  // Queries (used by MQTT adapter during update_erd / register_erd)
  // -------------------------------------------------------------------------

  /// Returns true if valid-ERD filtering is active.
  /// When false, all ERDs pass the filter.
  bool has_valid_erds_filter() const { return valid_erds_ready_; }

  /// Returns true if the ERD passes the valid-ERD filter (or no filter active).
  bool is_valid(tiny_erd_t erd) const;

  /// Returns true if the ERD value should be published as ASCII text.
  bool is_string_type(tiny_erd_t erd) const;

  // -------------------------------------------------------------------------
  // Read-only accessors (HaDiscoveryManager, diagnostics)
  // -------------------------------------------------------------------------

  const std::set<tiny_erd_t>& registered_erds() const { return registered_erds_; }
  const std::set<tiny_erd_t>& valid_erds()       const { return valid_erds_; }
  const std::set<tiny_erd_t>& string_erds()      const { return string_erds_; }

 private:
  std::set<tiny_erd_t> valid_erds_;
  std::set<tiny_erd_t> string_erds_;
  std::set<tiny_erd_t> registered_erds_;
  bool valid_erds_ready_{false};
};

}  // namespace geappliances_bridge
}  // namespace esphome
