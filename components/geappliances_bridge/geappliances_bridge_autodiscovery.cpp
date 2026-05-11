/*!
 * @file
 * @brief Appliance autodiscovery state machine.
 *
 * Sends GEA3 (then GEA2) broadcast reads of ERD 0x0008 (appliance type) to
 * find the first responding board on the bus.  On success it records the
 * board's address, selects the active ERD client, and hands off to the
 * feature-bit reading phase.
 */

#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG = "geappliances_bridge";

void GeappliancesBridge::on_mqtt_connected_()
{
  ESP_LOGI(TAG, "MQTT connected, flushing pending updates");

  // Flush any pending ERD updates that were queued while MQTT was disconnected.
  if (this->mqtt_bridge_initialized_) {
    esphome_mqtt_client_adapter_notify_connected(&this->mqtt_client_adapter_);
  }

  // Do NOT call notify_mqtt_disconnected_() here. Calling it on every MQTT
  // (re)connect was the primary crash source for GEA2 devices:
  //
  //   1. It sent signal_mqtt_disconnected to the HSM, forcing the polling
  //      bridge back to state_identify_appliance and triggering full GEA2
  //      re-identification (~3 s) plus feature-ERD re-reads (~1.75 s more).
  //
  //   2. The state_polling re-entry happened inside tiny_timer_group_run(),
  //      which is called from within the 200 ms GEA2 tight loop. On first
  //      boot (erd_set empty) this ran 56 subscribe() calls — each allocating
  //      heap — while the GEA2 loop blocked FreeRTOS context switches. This
  //      produced a 499 ms spike, 26 dropped MQTT SUBACK events, and a race
  //      with the IDF MQTT task that corrupted the heap.
  //
  // ESPHome's MQTT client automatically re-sends SUBSCRIBE packets for all
  // tracked topics when it reconnects, so no explicit resubscription is needed
  // here. ERD values queued during MQTT downtime are flushed above via
  // notify_connected. If the appliance is genuinely lost, signal_appliance_lost
  // fires after 60 s (polling) or signal_subscription_host_came_online handles
  // it (subscription bridge).

}

void GeappliancesBridge::notify_mqtt_disconnected_()
{
  if (this->mqtt_bridge_initialized_) {
    esphome_mqtt_client_adapter_notify_disconnected(&this->mqtt_client_adapter_);
  }
}

void GeappliancesBridge::run_autodiscovery_()
{
  switch (this->autodiscovery_state_) {
    case AUTODISCOVERY_WAITING_5S:
      // Unsigned subtraction wraps correctly after the ~49-day millis() rollover.
      if (millis() - this->autodiscovery_timer_start_ >= STARTUP_DELAY_MS) {
        ESP_LOGI(TAG, "5s delay complete, starting autodiscovery");
        // Prefer GEA3 when both UARTs are configured.
        if (this->uart_ != nullptr) {
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        } else {
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_GEA3_BROADCAST_PENDING: {
      this->gea3_board_discovered_ = false;
      tiny_gea3_erd_client_request_id_t req_id;
      if (tiny_gea3_erd_client_read(&this->erd_client_.interface, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA3 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
        this->autodiscovery_timer_start_ = millis();
        this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_WAITING;
      }
      // else: retry next loop iteration
      break;
    }

    case AUTODISCOVERY_GEA3_BROADCAST_WAITING:
      if (millis() - this->autodiscovery_timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->gea3_board_discovered_) {
          ESP_LOGI(TAG, "GEA3 board discovered at 0x%02X, autodiscovery complete",
                   this->host_address_);
          this->autodiscovery_state_ = AUTODISCOVERY_COMPLETE;
          this->start_device_id_generation_();
        } else if (this->gea2_uart_ != nullptr) {
          ESP_LOGW(TAG, "No GEA3 boards found, trying GEA2...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        } else {
          ESP_LOGW(TAG, "No GEA3 boards found, retrying GEA3...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_GEA2_BROADCAST_PENDING: {
      this->gea2_board_discovered_ = false;
      tiny_gea2_erd_client_request_id_t req_id;
      if (tiny_gea2_erd_client_read(&this->gea2_erd_client_.interface, &req_id,
                                     GEA_BROADCAST_ADDRESS, ERD_APPLIANCE_TYPE)) {
        ESP_LOGI(TAG, "Sent GEA2 broadcast (ERD 0x%04X) to address 0x%02X",
                 ERD_APPLIANCE_TYPE, GEA_BROADCAST_ADDRESS);
        this->autodiscovery_timer_start_ = millis();
        this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_WAITING;
      }
      // else: retry next loop iteration
      break;
    }

    case AUTODISCOVERY_GEA2_BROADCAST_WAITING:
      if (millis() - this->autodiscovery_timer_start_ >= AUTODISCOVERY_BROADCAST_WINDOW_MS) {
        if (this->gea2_board_discovered_) {
          ESP_LOGI(TAG, "GEA2 board discovered at 0x%02X, autodiscovery complete",
                   this->host_address_);
          this->autodiscovery_state_ = AUTODISCOVERY_COMPLETE;
          this->gea2_protocol_active_ = true;
          this->start_device_id_generation_();
        } else if (this->uart_ != nullptr) {
          ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA3...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA3_BROADCAST_PENDING;
        } else {
          ESP_LOGW(TAG, "No GEA2 boards found, retrying GEA2...");
          this->autodiscovery_state_ = AUTODISCOVERY_GEA2_BROADCAST_PENDING;
        }
      }
      break;

    case AUTODISCOVERY_COMPLETE:
      break;
  }
}

}  // namespace geappliances_bridge
}  // namespace esphome
