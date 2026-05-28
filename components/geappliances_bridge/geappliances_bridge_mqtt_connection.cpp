/*!
 * @file
 * @brief MQTT connection lifecycle handlers.
 *
 * Handles MQTT (re)connection and disconnection events.  On reconnect,
 * pending ERD updates are flushed.  On disconnect, the MQTT client adapter
 * and bridge HSMs are notified so they can enter their reconnect states.
 *
 * NOTE: Despite the filename prefix "geappliances_bridge_", this file does
 * NOT contain autodiscovery logic — that lives in autodiscovery_manager.cpp.
 */

#include "geappliances_bridge.h"
#include "geappliances_bridge_constants.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "geappliances_bridge";

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

}  // namespace geappliances_bridge
}  // namespace esphome
