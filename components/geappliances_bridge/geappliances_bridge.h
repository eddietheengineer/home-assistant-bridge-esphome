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

// Operation mode for the bridge
// Note: These enum values must match MODE_*_VALUE constants in __init__.py
enum BridgeMode {
  BRIDGE_MODE_POLL = 0,       // Always use polling mode
  BRIDGE_MODE_SUBSCRIBE = 1,  // Always use subscription mode
  BRIDGE_MODE_AUTO = 2        // Auto: try subscription, fallback to polling
};

class GeappliancesBridge : public Component {
  // Allow the startup HSM state functions to access protected members
  friend GeappliancesBridge* bridge_from_hsm(tiny_hsm_t* hsm);
  friend tiny_hsm_result_t startup_state_top(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_protocol_stack(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_autodiscovery(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_device_id(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_mqtt_client_init(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_feature_bits(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_bridge_init(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_subscription_watch(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_ha_discovery(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);
  friend tiny_hsm_result_t startup_state_running(
    tiny_hsm_t* hsm, tiny_hsm_signal_t signal, const void* data);

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
  void on_mqtt_connected_();
  void notify_mqtt_disconnected_();
  void handle_erd_client_activity_(const tiny_gea3_erd_client_on_activity_args_t* args);
  void initialize_mqtt_client_();
  void initialize_mqtt_bridge_();
  void start_custom_erd_polling_();
  void maybe_start_custom_erd_polling_();
  void configure_polling_optional_lists_();
  void check_subscription_activity_();
  // ── Per-phase run_*() methods called from loop() ─────────────────────────
  void run_protocol_stack_();       // Phase 0: drive GEA2/GEA3 hardware
  void log_poll_state_transitions_(); // Debug: log polling HSM state changes

  void start_feature_bit_reading_();
  // Device ID generation is now handled by DeviceIdentityManager -
  // the following are retained for backward compatibility during migration
  void start_device_id_generation_();
  void sync_feature_bit_legacy_members_();
  void sync_autodiscovery_legacy_members_();
  void sync_ha_discovery_legacy_members_();
  void on_ha_discovery_erd_seen_(tiny_erd_t erd);
  std::string bytes_to_string_(const uint8_t* data, size_t size);
  std::string sanitize_for_mqtt_topic_(const std::string& input);
  bool should_route_to_feature_bits_(tiny_erd_t erd);
  bool try_read_erd_with_retry_(tiny_erd_t erd, const char* erd_name);

  enum BridgeInitState {
    BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID,
    BRIDGE_INIT_STATE_WAITING_FOR_MQTT,
    BRIDGE_INIT_STATE_COMPLETE
  };

  // Startup HSM — replaces the manual switch-based phase progression.
  // The HSM drives the linear startup sequence:
  //   protocol_stack → autodiscovery → device_id → mqtt_client_init
  //                 → feature_bits → bridge_init → subscription_watch
  //                 → ha_discovery → running
  tiny_hsm_t startup_hsm_;

  uart::UARTComponent *uart_{nullptr};
  uart::UARTComponent *gea2_uart_{nullptr};
  std::string configured_device_id_;
  std::string generated_device_id_;
  std::string final_device_id_;
  uint8_t client_address_{0xE4};
  uint8_t host_address_{0xC0};       // Host address for ERD reads (0xC0 fallback; updated during autodiscovery)
  bool mqtt_was_connected_{false};
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
  static constexpr uint32_t SUBSCRIPTION_TIMEOUT_MS = 30000; // 30 seconds

  // Startup phase timeouts — prevent the startup HSM from stalling
  // indefinitely in any phase that waits for ERD reads.
  static constexpr uint32_t DEVICE_ID_PHASE_TIMEOUT_MS = 30000;  // 30 s
  static constexpr uint32_t FEATURE_BITS_PHASE_TIMEOUT_MS = 60000;  // 60 s
  uint32_t device_id_phase_start_ms_{0};
  uint32_t feature_bits_phase_start_ms_{0};

  // GEA2 tight-loop duration: covers the full TX→RX cycle at 19200 baud
  // (see doc/geappliances_bridge.md section 13 for detailed explanation)
  static constexpr uint32_t GEA2_LOOP_DURATION_MS = 200;
  bool gea2_protocol_active_{false}; // true once a GEA2 appliance is discovered

  // Device identity manager (extracted from god class)
  DeviceIdentityManager device_identity_manager_;

  // Feature bit manager (extracted from god class)
  FeatureBitManager feature_bit_manager_;

  // Legacy members retained for backward compatibility during migration -
  // these will be removed once all code paths use device_identity_manager_
  DeviceIdState device_id_state_{DEVICE_ID_STATE_IDLE};
  BridgeInitState bridge_init_state_{BRIDGE_INIT_STATE_WAITING_FOR_DEVICE_ID};

  // Feature bit reading state machine (runs after autodiscovery, before device ID gen)
  FeatureBitState feature_bit_state_{FEATURE_BIT_STATE_IDLE};
  uint8_t feature_bit_erd_0092_[8]{};  // raw bytes from ERD 0x0092 (common features)
  uint8_t feature_bit_erd_0093_[8]{};  // raw bytes from ERD 0x0093 (appliance APIs, group 0)
  uint8_t feature_bit_erd_0094_[8]{};  // raw bytes from ERD 0x0094 (appliance APIs, group 1)
  uint8_t feature_bit_erd_0095_[8]{};  // raw bytes from ERD 0x0095 (appliance APIs, group 2)
  uint8_t feature_bit_erd_0096_[8]{};  // raw bytes from ERD 0x0096 (appliance APIs, group 3)
  uint8_t feature_bit_erd_0097_[8]{};  // raw bytes from ERD 0x0097 (appliance APIs, group 4)
  uint8_t feature_bit_erd_0109_[8]{};  // raw bytes from ERD 0x0109 (appliance APIs, group 5)
  uint8_t feature_bit_erd_010A_[8]{};  // raw bytes from ERD 0x010A (appliance APIs, group 6)
  uint8_t feature_bit_erd_010B_[8]{};  // raw bytes from ERD 0x010B (appliance APIs, group 7)
  uint8_t feature_bit_erd_010C_[8]{};  // raw bytes from ERD 0x010C (appliance APIs, group 8)
  uint8_t feature_bit_erd_010D_[8]{};  // raw bytes from ERD 0x010D (appliance APIs, group 9)
  uint8_t feature_bit_erd_0092_size_{0};
  uint8_t feature_bit_erd_0093_size_{0};
  uint8_t feature_bit_erd_0094_size_{0};
  uint8_t feature_bit_erd_0095_size_{0};
  uint8_t feature_bit_erd_0096_size_{0};
  uint8_t feature_bit_erd_0097_size_{0};
  uint8_t feature_bit_erd_0109_size_{0};
  uint8_t feature_bit_erd_010A_size_{0};
  uint8_t feature_bit_erd_010B_size_{0};
  uint8_t feature_bit_erd_010C_size_{0};
  uint8_t feature_bit_erd_010D_size_{0};
  // Set of valid ERDs built from parsed feature bits; used when appliance_api_parsing_ is true
  std::set<tiny_erd_t> appliance_api_valid_erds_;
  // Sorted vector of the same set, for passing to the polling bridge as a C array
  std::vector<tiny_erd_t> appliance_api_valid_erds_vec_;
  bool appliance_api_valid_list_ready_{false};

  // HA device discovery publish: deferred until ERD registration has settled.
  // In subscription mode: publish 10 s after the last NEW ERD subscription
  //   publication is received (or 30 s from bridge init as a safety cap).
  // In polling mode: publish 10 s after the last ERD is registered by the
  //   polling bridge (tracked by comparing ha_registered_erds_.size() each
  //   loop iteration — the same 30 s cap applies).
  bool ha_discovery_pending_{false};
  bool ha_discovery_published_{false};
  bool ha_discovery_publish_in_progress_{false};
  uint32_t ha_discovery_last_activity_{0};
  const char* last_logged_poll_state_{nullptr};
  std::set<tiny_erd_t> ha_registered_erds_;
  std::set<tiny_erd_t> ha_string_erds_set_;

  // Base URL for the per-category JSONL files.
  // Can be overridden in YAML via ha_discovery_base_url.
  std::string ha_discovery_base_url_{
    "https://raw.githubusercontent.com/joshualongenecker/"
    "home-assistant-bridge-esphome/main/ha_discovery"
  };

  // Autodiscovery manager (extracted from god class)
  AutodiscoveryManager autodiscovery_manager_;

  // HA discovery manager (extracted from god class)
  HaDiscoveryManager ha_discovery_manager_;

  // Legacy members retained for backward compatibility during migration -
  // these will be removed once all code paths use autodiscovery_manager_
  AutodiscoveryState autodiscovery_state_{AUTODISCOVERY_WAITING_5S};
  uint32_t autodiscovery_retry_count_{0};

  tiny_gea3_erd_client_request_id_t pending_request_id_;
  uint8_t appliance_type_{0};
  std::string model_number_;
  std::string serial_number_;
  uint32_t queue_retry_count_{0};
  static constexpr uint32_t LOG_EVERY_N_RETRIES = 50; // Log retry attempts periodically
  static constexpr uint32_t MAX_QUEUE_RETRIES = 1000; // Maximum retries before giving up (about 10 seconds at loop rate)

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
  uint8_t client_queue_buffer_[2048];

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

  i_tiny_gea3_erd_client_t* active_erd_client_{nullptr}; // set during initialize_mqtt_bridge_()

  mqtt_bridge_t mqtt_bridge_;
  mqtt_bridge_polling_t mqtt_bridge_polling_;

  tiny_event_subscription_t erd_client_activity_subscription_;
  tiny_event_subscription_t gea2_activity_subscription_;
};

}  // namespace geappliances_bridge
}  // namespace esphome
