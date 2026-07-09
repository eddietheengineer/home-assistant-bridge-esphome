#include "ota_cleanup_manager.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome_time_source.h"
#include "geappliances_bridge_log.h"

#ifndef USE_ESP_IDF
#error "This component requires ESPHome with framework: type: esp-idf"
#endif

#if defined(USE_ESP_IDF) && !defined(USE_ESP_IDF_STUBS)
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esphome/core/preferences.h"
#elif defined(USE_ESP_IDF_STUBS)
#include "esp-idf/esp_task_wdt.h"
#endif

GEA_TAG(TAG) = "ota_cleanup_manager";

namespace esphome {
namespace geappliances_bridge {

void OtaCleanupManager::init(
    ha_discovery_manager_t* ha_discovery_manager,
    DeviceIdentityManager& device_identity_manager,
    esphome_mqtt_client_adapter_t& mqtt_client_adapter,
    erd_cache_t* erd_cache,
    bool generate_device_config,
    bool filter_config_topics,
    bool& steady_state_reached,
    bool& mqtt_initialized,
    std::function<void()> reboot_callback)
{
  this->ha_discovery_manager_ = ha_discovery_manager;
  this->device_identity_manager_ = &device_identity_manager;
  this->mqtt_client_adapter_ = &mqtt_client_adapter;
  this->erd_cache_ = erd_cache;
  this->generate_device_config_ = generate_device_config;
  this->filter_config_topics_ = filter_config_topics;
  this->steady_state_reached_ = &steady_state_reached;
  this->mqtt_initialized_ = &mqtt_initialized;
  this->reboot_callback_ = std::move(reboot_callback);
}

void OtaCleanupManager::trigger_ota_cleanup() {
  this->ota_cleanup_needed_ = true;
  this->cleanup_trigger_ = CleanupTrigger::OTA;
}

void OtaCleanupManager::trigger_discovery_refresh() {
  if (this->discovery_refresh_in_progress_ ||
      this->ota_cleanup_needed_ ||
      this->ota_cleanup_in_progress_ ||
      this->ota_discovery_publishing_ ||
      this->ota_reboot_pending_) {
    ESP_LOGW(TAG, "Discovery refresh already in progress, ignoring");
    return;
  }
  this->discovery_refresh_in_progress_ = true;
  this->cleanup_trigger_ = CleanupTrigger::DISCOVERY_REFRESH;
  ESP_LOGI(TAG, "Discovery refresh queued, will execute when appliance is ready");
}

bool OtaCleanupManager::is_ready() const {
  return this->steady_state_reached_ != nullptr &&
         *this->steady_state_reached_ &&
         this->mqtt_initialized_ != nullptr &&
         *this->mqtt_initialized_ &&
         this->device_identity_manager_ != nullptr &&
         this->device_identity_manager_->get_state() == DEVICE_ID_STATE_COMPLETE;
}

bool OtaCleanupManager::start_cleanup_()
{
  if (!this->is_ready()) {
    return false;
  }
  ha_discovery_cleanup_configure(&this->ha_discovery_manager_->cleanup,
      this->device_identity_manager_->get_device_id(),
      &this->mqtt_client_adapter_->interface, esphome::millis);
  ha_discovery_cleanup_start(&this->ha_discovery_manager_->cleanup);
  this->ota_cleanup_in_progress_ = true;
  return true;
}

void OtaCleanupManager::loop() {
#if defined(USE_ESP_IDF) && !defined(USE_ESP_IDF_STUBS)
  // ── Start cleanup (OTA or DiscoveryRefresh) ──────────────────────────────
  if (!this->ota_cleanup_in_progress_ &&
      !this->ota_discovery_publishing_ && !this->ota_reboot_pending_) {
    bool ota_trigger = this->generate_device_config_ && this->ota_cleanup_needed_;
    bool refresh_trigger = this->discovery_refresh_in_progress_;

    if (ota_trigger || refresh_trigger) {
      if (this->start_cleanup_()) {
        if (ota_trigger) {
          ESP_LOGI(TAG, "Starting OTA-triggered HA discovery cleanup...");
        }
        if (refresh_trigger) {
          this->discovery_refresh_in_progress_ = false;
        }
      }
    }
  }

  // ── Drive cleanup (shared by OTA and DiscoveryRefresh paths) ─────────────
  if (this->ota_cleanup_in_progress_) {
    ha_discovery_cleanup_run(&this->ha_discovery_manager_->cleanup);
    if (ha_discovery_cleanup_is_done(&this->ha_discovery_manager_->cleanup)) {
      this->ota_cleanup_in_progress_ = false;
      if (this->cleanup_trigger_ == CleanupTrigger::OTA) {
        this->ota_cleanup_needed_ = false;
      }
      ESP_LOGI(TAG, "HA discovery cleanup complete, publishing fresh discovery...");
      ha_discovery_cleanup_destroy(&this->ha_discovery_manager_->cleanup);
      ha_discovery_manager_init(this->ha_discovery_manager_);

      // Publish fresh HA discovery payloads.
      ha_discovery_manager_configure(
        this->ha_discovery_manager_,
        this->device_identity_manager_->get_device_id(),
        this->device_identity_manager_->get_model_number(),
        this->device_identity_manager_->get_serial_number(),
        this->device_identity_manager_->get_appliance_type(),
        this->filter_config_topics_,
        this->erd_cache_,
        &this->mqtt_client_adapter_->interface);
      ha_discovery_manager_start(this->ha_discovery_manager_);
      this->ota_discovery_publishing_ = true;
    }
  }

  // ── Drive OTA discovery publishing ───────────────────────────────────────
  if (this->ota_discovery_publishing_) {
    if (ha_discovery_manager_is_processing(this->ha_discovery_manager_)) {
      ha_discovery_manager_run(this->ha_discovery_manager_);
    } else {
      this->ota_discovery_publishing_ = false;
      this->cleanup_trigger_ = CleanupTrigger::NONE;
      ESP_LOGI(TAG, "OTA HA discovery publish complete, preparing reboot...");

      // Clear safe mode counter and mark OTA valid before reboot.
      uint32_t val = 0;
      static constexpr uint32_t SAFE_MODE_RTC_KEY = 233825507UL;
      ESPPreferenceObject rtc_pref = global_preferences->make_preference<uint32_t>(SAFE_MODE_RTC_KEY, false);
      rtc_pref.save(&val);
      global_preferences->sync();
      esp_ota_mark_app_valid_cancel_rollback();
      ESP_LOGI(TAG, "Safe mode counter cleared, OTA rollback cancelled");

      this->ota_reboot_pending_ = true;
      this->ota_reboot_start_ms_ = esphome::millis();
    }
  }

  // ── Wait then reboot ─────────────────────────────────────────────────────
  if (this->ota_reboot_pending_) {
    esp_task_wdt_reset();
    uint32_t elapsed = esphome::millis() - this->ota_reboot_start_ms_;
    if (elapsed >= 5000) {
      ESP_LOGI(TAG, "Rebooting after OTA cleanup + republish...");
      this->ota_reboot_pending_ = false;
      this->reboot_callback_();
    }
  }
#endif
}

}  // namespace geappliances_bridge
}  // namespace esphome