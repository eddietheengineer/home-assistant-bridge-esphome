/*
 * @file
 * @brief DeviceIdentityManager implementation.
 *
 * Self-driving: on_erd_read_completed() queues the next ERD read,
 * on_erd_read_failed() re-queues the current ERD read.
 */

#include "device_identity_manager.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"
#include <cstring>
#include <inttypes.h>

// Forward declaration (generated from appliance API data)
std::string appliance_type_to_string(uint8_t appliance_type);

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "device_identity";

void DeviceIdentityManager::init(const std::string& configured_id,
                                  i_tiny_gea3_erd_client_t* erd_client,
                                  uint8_t host_address)
{
  this->configured_device_id_ = configured_id;
  this->erd_client_ = erd_client;
  this->host_address_ = host_address;
  this->state_ = DEVICE_ID_STATE_READING_APPLIANCE_TYPE;

  if (!this->configured_device_id_.empty()) {
    ESP_LOGI(TAG, "Device ID configured: %s (will still read identity ERDs)",
             this->configured_device_id_.c_str());
  } else {
    ESP_LOGI(TAG, "No device_id configured, will auto-generate from identity ERDs");
  }

  // Immediately queue the first ERD read
  this->try_queue_read_(ERD_APPLIANCE_TYPE);
}

void DeviceIdentityManager::on_erd_read_completed(tiny_erd_t erd, const uint8_t* data, uint8_t size)
{
  if (erd == ERD_APPLIANCE_TYPE) {
    if (size < 1) return;
    this->appliance_type_ = data[0];
    ESP_LOGI(TAG, "Read appliance type: %u", this->appliance_type_);
    this->state_ = DEVICE_ID_STATE_READING_MODEL_NUMBER;
    this->try_queue_read_(ERD_MODEL_NUMBER);

  } else if (erd == ERD_MODEL_NUMBER) {
    this->model_number_ = this->bytes_to_string_(data, size);
    ESP_LOGI(TAG, "Read model number: %s", this->model_number_.c_str());
    this->state_ = DEVICE_ID_STATE_READING_SERIAL_NUMBER;
    this->try_queue_read_(ERD_SERIAL_NUMBER);

  } else if (erd == ERD_SERIAL_NUMBER) {
    this->serial_number_ = this->bytes_to_string_(data, size);
    ESP_LOGI(TAG, "Read serial number: %s", this->serial_number_.c_str());
    this->generated_device_id_ = appliance_type_to_string(this->appliance_type_) + "_" +
                                  this->sanitize_for_mqtt_topic_(this->model_number_) + "_" +
                                  this->sanitize_for_mqtt_topic_(this->serial_number_);
    ESP_LOGI(TAG, "Generated device ID: %s", this->generated_device_id_.c_str());
    this->state_ = DEVICE_ID_STATE_COMPLETE;
  }
}

void DeviceIdentityManager::on_erd_read_failed(tiny_erd_t erd)
{
  // Stay in the current state and immediately re-queue the ERD read.
  // Retries indefinitely -- never give up.
  ESP_LOGW(TAG, "Failed to read ERD 0x%04X for device ID generation, retrying", erd);
  this->try_queue_read_(erd);
}

const std::string& DeviceIdentityManager::get_device_id() const
{
  if (!this->configured_device_id_.empty()) {
    return this->configured_device_id_;
  }
  return this->generated_device_id_;
}

bool DeviceIdentityManager::try_queue_read_(tiny_erd_t erd)
{
  if (this->erd_client_ == nullptr) {
    return false;
  }

  return tiny_gea3_erd_client_read(this->erd_client_, &this->pending_request_id_,
                                    this->host_address_, erd);
}

std::string DeviceIdentityManager::bytes_to_string_(const uint8_t* data, size_t size)
{
  if (size == 0) return "";
  std::string result(reinterpret_cast<const char*>(data), size);
  // Trim trailing null bytes
  while (!result.empty() && result.back() == '\0') {
    result.pop_back();
  }
  return result;
}

std::string DeviceIdentityManager::sanitize_for_mqtt_topic_(const std::string& input)
{
  std::string result;
  result.reserve(input.size());
  for (char c : input) {
    if (c == '+' || c == '#' || c == '/' || c == '$' || c == ' ' ||
        static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
      result += '_';
    } else {
      result += c;
    }
  }
  return result.empty() ? "Unknown" : result;
}

}  // namespace geappliances_bridge
}  // namespace esphome
