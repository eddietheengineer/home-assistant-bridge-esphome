// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Coordinate the lifecycle of all bridge components and serve as the
//       ESPHome component entry point (setup / loop / dump_config).
//
// Responsibilities:
//   - Own and construct all component instances (adapters, managers, bridges)
//   - Wire components together during setup()
//   - Drive the GEA2 tight-loop and delegate ongoing work in loop()
//   - Expose configuration setters called by the ESPHome code generator
//   - Implement IBridgeServices so the startup HSM can request bridge actions
//     without depending on this concrete class
//
// NOT responsible for:
//   - Assembling the device ID (DeviceIdentityManager)
//   - Determining which ERDs are valid (FeatureBitManager / ErdRegistry)
//   - Publishing HA discovery payloads (HaDiscoveryManager)
//   - MQTT connection lifecycle (EsphomeMqttClientAdapter)
//   - Startup phase sequencing (StartupHsm)
//
// Dependencies:
//   - ESPHome UART, MQTT, and Component APIs
//   - tiny_gea3_interface, tiny_gea2_interface, tiny_gea3_erd_client
//   - All manager and adapter classes in this component
// =============================================================================

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <string>
#include <set>
#include <vector>

extern "C" {
#include "mqtt_bridge.h"
#include "mqtt_bridge_polling.h"
#include "tiny_gea3_erd_client.h"
#include "tiny_gea3_interface.h"
#include "tiny_gea2_erd_client.h"
#include "tiny_gea2_interface.h"
#include "tiny_timer.h"
#include "tiny_hsm.h"
}

#include "gea2_erd_client_adapter.h"

#include "esphome_uart_adapter.h"
#include "bridge_mode.h"
#include "i_bridge_services.h"
#include "erd_registry.h"
#include "esphome_mqtt_client_adapter.h"
#include "device_identity_manager.h"
#include "feature_bit_manager.h"
#include "autodiscovery_manager.h"
#include "ha_discovery_manager.h"
#include "geappliances_bridge_startup_hsm.h"

// Forward declaration of the generated function
std::string appliance_type_to_string(uint8_t appliance_type);

namespace esphome {
namespace geappliances_bridge {

// BridgeMode is now defined in bridge_mode.h (included via i_bridge_services.h).

class GeappliancesBridge : public Component, public IBridgeServices {

 public:
  static constexpr unsigned long baud = 230400;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  bool teardown() override;

  void set_gea3_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_gea2_uart(uart::UARTComponent *uart) { this->gea2_uart_ = uart; }
  void set_client_address(uint8_t address) { this->client_address_ = address; }
  void set_device_id(const std::string &device_id) { this->configured_device_id_ = device_id; }
  void set_mode(uint8_t mode) { this->mode_ = static_cast<BridgeMode>(mode); }
  void set_polling_interval(uint32_t polling_interval) { this->polling_interval_ms_ = polling_interval; }
  void set_polling_only_publish_on_change(bool only_publish_on_change) { this->polling_only_publish_on_change_ = only_publish_on_change; }
  void set_appliance_api_parsing(bool appliance_api_parsing) { this->appliance_api_parsing_ = appliance_api_parsing; }
  void set_generate_device_config(bool generate_device_config) { this->generate_device_config_ = generate_device_config; }
  void add_custom_erd(uint16_t erd) { this->custom_erds_vec_.push_back(static_cast<tiny_erd_t>(erd)); }
  void set_ha_discovery_base_url(const std::string& url) { this->ha_discovery_base_url_ = url; }



 protected:
  // ── IBridgeServices implementation (called exclusively by the startup HSM) ──
  void run_autodiscovery() override;
  bool is_autodiscovery_complete() const override;
  uint8_t get_discovered_host_address() const override;
  bool is_discovered_gea2_protocol() const override;

  void init_device_id_reading() override;
  bool is_device_id_complete() const override;

  bool is_mqtt_client_initialized() const override;
  void initialize_mqtt_client() override;

  void start_feature_bit_reading() override;
  bool is_feature_bits_complete() const override;

  void record_startup_delay_start() override;
  bool is_startup_delay_elapsed() const override;

  bool is_bridge_initialized() const override;
  void initialize_mqtt_bridge() override;

  BridgeMode get_mode() const override;
  bool is_subscription_mode_active() const override;

  void check_subscription_activity() override;
  void maybe_start_custom_erd_polling() override;
  void log_poll_state_transitions() override;
  void run_ha_discovery() override;
  void run_all_managers() override;

  // ── Internal bridge methods (event callbacks and per-phase helpers) ─────────
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_client_();
  void initialize_mqtt_bridge_();
  void start_custom_erd_polling_();
  void maybe_start_custom_erd_polling_();
  void configure_polling_optional_lists_();
  void check_subscription_activity_();
  void run_protocol_stack_();         // Drive GEA2/GEA3 hardware stack
  void log_poll_state_transitions_(); // Debug: log polling HSM state changes
  void start_feature_bit_reading_();
  void on_ha_discovery_erd_seen_(tiny_erd_t erd);
  bool should_route_to_feature_bits_(tiny_erd_t erd);

  // Startup HSM — replaces the manual switch-based phase progression.
  // The HSM drives the linear startup sequence:
  //   protocol_stack → autodiscovery → device_id → mqtt_client_init
  //                 → feature_bits → bridge_init → subscription_watch
  //                 → ha_discovery → running
  tiny_hsm_t startup_hsm_;

  uart::UARTComponent *uart_{nullptr};
  uart::UARTComponent *gea2_uart_{nullptr};
  std::string configured_device_id_;
  uint8_t client_address_{0xE4};

  // States for the non-blocking MQTT (re)connection FSM in loop().
  enum class MqttConnectionState : uint8_t {
    DISCONNECTED,  // No MQTT connection (or not yet seen)
    SUBSCRIBING,   // Connected; waiting for adapter init to subscribe wildcard
    FLUSHING,      // Subscribed; draining pending ERD update queue
    RUNNING,       // Steady-state: queue empty, draining new updates each loop
  };
  MqttConnectionState mqtt_connection_state_{MqttConnectionState::DISCONNECTED};
  bool mqtt_client_adapter_initialized_{false};
  bool mqtt_bridge_initialized_{false};
  BridgeMode mode_{BRIDGE_MODE_AUTO};
  uint32_t polling_interval_ms_{10000};
  bool polling_only_publish_on_change_{false};
  bool appliance_api_parsing_{true};
  bool generate_device_config_{false};
  // User-configured custom ERDs to poll in addition to the standard list.
  // Populated by add_custom_erd() calls generated from the YAML custom_erds option.
  std::vector<tiny_erd_t> custom_erds_vec_;

  // Auto mode fallback tracking
  bool subscription_mode_active_{false};
  bool subscription_activity_detected_{false};
  uint32_t subscription_start_time_{0};
  uint32_t custom_erd_subscription_last_activity_{0};
  std::set<tiny_erd_t> custom_erd_subscription_seen_erds_;
  bool custom_erd_polling_started_{false};  // Guard to prevent re-initialization
  static constexpr uint32_t SUBSCRIPTION_TIMEOUT_MS = 10000; // 10 seconds

  // Startup phase delay tracking
  uint32_t startup_delay_start_ms_{0};

  // GEA2 tight-loop duration: covers the full TX→RX cycle at 19200 baud
  // (see doc/geappliances_bridge.md section 13 for detailed explanation)
  static constexpr uint32_t GEA2_LOOP_DURATION_MS = 200;
  bool gea2_protocol_active_{false}; // fallback for manual device_id when autodiscovery is skipped

  // Device identity manager (extracted from god class)
  DeviceIdentityManager device_identity_manager_;

  // Feature bit manager (extracted from god class)
  FeatureBitManager feature_bit_manager_;

  // Guard to prevent re-initializing feature bit reading mid-sequence.
  // start_feature_bit_reading_() checks this flag so it won't re-init()
  // if called again while the first ERD read is still in-flight.
  bool feature_bit_reading_started_{false};

  // Feature bit reading state machine (runs after autodiscovery, before device ID gen)
  // The FeatureBitManager owns the valid ERD list and ready flag; use its getters directly.

  // HA device discovery state is managed by HaDiscoveryManager; the bridge
  // delegates to it rather than maintaining redundant copies.
  const char* last_logged_poll_state_{nullptr};
  // ERD registry: single owner of valid-ERD filter, string-type set,
  // and runtime registered-ERD tracking.
  ErdRegistry erd_registry_;

  // Base URL for the per-category JSONL files.
  // Can be overridden in YAML via ha_discovery_base_url.
  // Uses HEAD to always resolve against the repository's default branch.
  std::string ha_discovery_base_url_{
    "https://raw.githubusercontent.com/joshualongenecker/"
    "home-assistant-bridge-esphome/HEAD/ha_discovery"
  };

  // Autodiscovery manager (extracted from god class)
  AutodiscoveryManager autodiscovery_manager_;

  // HA discovery manager (extracted from god class)
  HaDiscoveryManager ha_discovery_manager_;

  tiny_timer_group_t timer_group_;

  // GEA3 components
  esphome_uart_adapter_t uart_adapter_;
  esphome_mqtt_client_adapter_t mqtt_client_adapter_;

  tiny_gea3_interface_t gea3_interface_;
  uint8_t receive_buffer_[255];
  uint8_t send_queue_buffer_[1000];

  tiny_gea3_erd_client_t erd_client_;
  /* GEA3 client queue buffer — sized to hold enough read requests for the
   * custom-ERD polling bridge (up to ~31 reads × 6 bytes each ≈ 188 bytes)
   * plus in-flight subscription acknowledgments and write requests.
   * Increased from 1024 to 2048 to prevent ring-buffer overflow when the
   * polling bridge and subscription bridge share the same ERD client;
   * overflow corrupts adjacent heap metadata causing
   * prvCheckTasksWaitingTermination crashes (see mqtt_bridge_polling.cpp). */
  uint8_t client_queue_buffer_[8192];

  // GEA2 components (only used when gea2_uart_ is set)
  esphome_uart_adapter_t gea2_uart_adapter_;

  tiny_gea2_interface_t gea2_interface_;
  uint8_t gea2_receive_buffer_[255];
  uint8_t gea2_send_queue_buffer_[10000];

  tiny_gea2_erd_client_t gea2_erd_client_;
  uint8_t gea2_client_queue_buffer_[8096];

  // Event fired once per millisecond to drive GEA2 interface's internal timers.
  // Published manually inside the GEA2 tight loop (not via a timer_group_ periodic
  // timer) so the 1 ms interrupt never fires in the GEA3 single-pass path and
  // cannot starve the GEA3/polling-bridge timers in the shared timer_group_.
  tiny_event_t gea2_msec_interrupt_;

  // Adapter that wraps the GEA2 ERD client as a GEA3 ERD client interface
  gea2_erd_client_adapter_t gea2_erd_client_adapter_;

  mqtt_bridge_t mqtt_bridge_;
  mqtt_bridge_polling_t mqtt_bridge_polling_;

  // Track which bridge(s) were actually initialized so teardown is unambiguous.
  // A subscription bridge (mqtt_bridge_) is created when use_polling is false.
  // A polling bridge (mqtt_bridge_polling_) is created when use_polling is true,
  // or when custom ERD polling is started alongside a subscription bridge.
  bool subscription_bridge_initialized_{false};
  bool polling_bridge_initialized_{false};

  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_event_subscription_t gea2_activity_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
