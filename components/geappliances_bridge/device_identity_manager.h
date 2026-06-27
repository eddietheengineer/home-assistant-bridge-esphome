/*
 * @file
 * @brief Device identity manager for the GEA bridge.
 *
 * Handles reading appliance type, model number, and serial number ERDs,
 * then assembles a unique MQTT-topic-safe device identifier string.
 * Self-driving: queues reads internally via callbacks.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Read appliance identity ERDs and assemble a stable, MQTT-topic-safe
//       unique device identifier string.
//
// Responsibilities:
//   - Read ERDs 0x0008 (appliance type), 0x0001 (model number),
//     0x0002 (serial number) in sequence, retrying indefinitely on
//     failure until each reads successfully
//   - Sanitize raw values into MQTT-safe strings
//   - Concatenate into a final device ID
//   - Always reads ERDs even when a device_id is pre-configured in YAML;
//     the preconfigured value is only used by get_device_id() at the end
//
// NOT responsible for:
//   - Configuring the MQTT adapter with the resulting ID (caller's job)
//   - Managing bridge lifecycle
//   - Any ERDs beyond the three identity ERDs
//
// Dependencies:
//   - i_tiny_gea3_erd_client
//   - appliance_type_to_string() (generated)
// =============================================================================

#ifndef DEVICE_IDENTITY_MANAGER_H
#define DEVICE_IDENTITY_MANAGER_H

#include <string>
#include <cstdint>

extern "C" {
#include "tiny_gea3_erd_client.h"
}

namespace esphome {
namespace geappliances_bridge {

// Device ID generation states
enum DeviceIdState {
  DEVICE_ID_STATE_READING_APPLIANCE_TYPE,
  DEVICE_ID_STATE_READING_MODEL_NUMBER,
  DEVICE_ID_STATE_READING_SERIAL_NUMBER,
  DEVICE_ID_STATE_COMPLETE,
};

/*
 * Manages device ID generation from appliance identity ERDs.
 *
 * Reads ERDs 0x0008 (appliance type), 0x0001 (model number), and 0x0002
 * (serial number), then assembles a unique device identifier. Each ERD is
 * retried indefinitely on failure -- the manager never moves on until it
 * has successfully read all three. The manager is fully self-driving:
 * on_erd_read_completed() queues the next read, and on_erd_read_failed()
 * re-queues the current read.
 */
class DeviceIdentityManager {
 public:
  /*
   * Initialize with configured device ID (if any), ERD client, and host address.
   * Always starts reading ERDs regardless of whether a configured ID exists.
   */
  void init(const char* configured_id,
            i_tiny_gea3_erd_client_t* erd_client,
            uint8_t host_address);

  /*
   * Called when an ERD read completes successfully during device ID generation.
   * Transitions to the next state and immediately queues the next ERD read.
   */
  void on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size);

  /*
   * Called when an ERD read fails during device ID generation. Stays in the
   * current state and immediately re-queues the ERD read. Retries indefinitely.
   */
  void on_erd_read_failed(tiny_erd_t erd);

  /*
   * Get the current state.
   */
  DeviceIdState get_state() const { return state_; }

  /*
   * Get the final device ID string. Returns the preconfigured ID if one was
   * provided, otherwise the auto-generated ID.
   */
  const char* get_device_id() const;

  /*
   * Get the model number string.
   */
  const char* get_model_number() const { return model_number_; }
  /*
   * Get the appliance type byte (from ERD 0x0008).
   */
  uint8_t get_appliance_type() const { return appliance_type_; }

  /*
   * Get the serial number string.
   */
  const char* get_serial_number() const { return serial_number_; }

 private:
  bool try_queue_read_(tiny_erd_t erd);
  void bytes_to_string_(const uint8_t* data, size_t size, char* out, size_t out_size);
  std::string sanitize_for_mqtt_topic_(const char* input);

  DeviceIdState state_{DEVICE_ID_STATE_READING_APPLIANCE_TYPE};
  bool has_configured_device_id_{false};
  char configured_device_id_[64];
  char generated_device_id_[64];
  uint8_t appliance_type_{0};
  char model_number_[64];
  char serial_number_[64];
  tiny_gea3_erd_client_request_id_t pending_request_id_{0};

  i_tiny_gea3_erd_client_t* erd_client_{nullptr};
  uint8_t host_address_{0};
};

}  // namespace geappliances_bridge
}  // namespace esphome

#endif  // DEVICE_IDENTITY_MANAGER_H
