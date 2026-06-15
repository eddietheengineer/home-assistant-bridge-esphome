/*!
 * @file
 * @brief HaDiscoveryManager implementation.
 */

#include "ha_discovery_manager.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "ha_discovery";

void HaDiscoveryManager::init(const std::string& base_url,
                              const std::string& device_id,
                              const std::string& model_number,
                              const std::string& serial_number,
                              const std::set<tiny_erd_t>& registered_erds,
                              bool generate_device_config)
{
  this->base_url_               = base_url;
  this->device_id_              = device_id;
  this->model_number_           = model_number;
  this->serial_number_          = serial_number;
  this->registered_erds_        = registered_erds;
  this->generate_device_config_ = generate_device_config;
  this->state_                  = HA_DISCOVERY_WAITING_FOR_READY;
  this->last_activity_          = millis();
  this->start_time_             = millis();
}

void HaDiscoveryManager::set_registered_erds(const std::set<tiny_erd_t>& erds)
{
  this->registered_erds_ = erds;
}

void HaDiscoveryManager::on_erd_seen(tiny_erd_t erd)
{
  if (this->state_ != HA_DISCOVERY_WAITING_FOR_READY) return;
  if (this->seen_erds_.find(erd) == this->seen_erds_.end()) {
    this->seen_erds_.insert(erd);
    this->last_activity_ = millis();
  }
}

void HaDiscoveryManager::set_mqtt_adapter(esphome_mqtt_client_adapter_t* mqtt_adapter)
{
  this->mqtt_adapter_ = mqtt_adapter;
}

void HaDiscoveryManager::cleanup()
{
#ifdef USE_ESP_IDF
  // If a fetch task is still running, signal it to stop via the sentinel.
  if (this->queue_ != nullptr) {
    HaDiscoveryItem* sentinel = nullptr;
    // Non-blocking send — if queue is full, the task will get the sentinel
    // after it drains existing items.
    xQueueSend(this->queue_, &sentinel, 0);

    // Drain all remaining items from the queue before waiting for the task.
    // If the fetch task is blocked on xQueueSend() (queue full), this
    // unblocks it so it can finish and call vTaskDelete().
    {
      HaDiscoveryItem* item = nullptr;
      while (xQueueReceive(this->queue_, &item, 0) == pdTRUE) {
        if (item != nullptr) delete item;
      }
    }
    // Re-send the sentinel now that space is guaranteed.
    xQueueSend(this->queue_, &sentinel, 0);

    // Wait for the task to actually terminate before freeing its stack/TCB.
    // Without this, freeing the stack while the task is still executing
    // causes a use-after-free crash.
    if (this->task_handle_ != nullptr) {
      // Poll with a generous timeout (up to 5 s) to wait for the task
      // to call vTaskDelete().  The task deletes itself after sending the
      // sentinel to the queue, so we wait until the handle becomes NULL.
      // Use subtraction to avoid millis() overflow (deadline = start + 5000
      // wraps incorrectly when millis() is near UINT32_MAX).
      uint32_t start = millis();
      while (this->task_handle_ != nullptr && millis() - start < 5000) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (this->task_handle_ != nullptr) {
        ESP_LOGW(TAG, "HA discovery task did not terminate within 5 s");
      }
    }
  }
  // Free heap-allocated stack and TCB (allocated in publish_ha_discovery_).
  // Safe to free now — the task has terminated (or timed out).
  if (this->task_stack_ != nullptr) {
    free(this->task_stack_);
    this->task_stack_ = nullptr;
  }
  if (this->task_tcb_ != nullptr) {
    free(this->task_tcb_);
    this->task_tcb_ = nullptr;
  }
  // Delete the queue.
  if (this->queue_ != nullptr) {
    vQueueDelete(this->queue_);
    this->queue_ = nullptr;
  }
  this->task_handle_ = nullptr;
#endif
}

void HaDiscoveryManager::run(bool is_poll_mode,
                             bool polling_bridge_initialized,
                             bool polling_list_complete,
                             bool subscription_activity_detected,
                             mqtt::MQTTClientComponent* mqtt_client)
{
  if (this->state_ == HA_DISCOVERY_WAITING_FOR_READY) {
    bool ready = false;
    if (is_poll_mode) {
      ready = polling_list_complete;
    } else {
      // Subscription mode (or auto mode with subscription active):
      // ready once the quiet window elapses after the last new ERD was seen.
      bool quiet = false;
      if (subscription_activity_detected) {
        if (millis() - this->last_activity_ >= HA_DISCOVERY_QUIET_MS) {
          quiet = true;
        }
      }
      // Safety cap: start discovery after 30 s even if activity never
      // settles, so HA discovery is never permanently blocked.
      if (millis() - this->start_time_ >= HA_DISCOVERY_MAX_WAIT_MS) {
        quiet = true;
      }
      // In auto mode, if the polling bridge is also active, gate on
      // polling discovery completion as well (spec 3.3).
      if (quiet && polling_bridge_initialized) {
        ready = polling_list_complete;
      } else if (quiet) {
        ready = true;
      }
    }
    if (ready) {
      this->publish_ha_discovery_(mqtt_client);
    }
  }

  if (this->state_ == HA_DISCOVERY_PUBLISHING) {
    uint32_t now = millis();
    if (now - this->last_publish_ms_ >= HA_ENTITY_PUBLISH_INTERVAL_MS) {
      this->last_publish_ms_ = now;
      this->publish_next_entity_(mqtt_client);
    }
  }
}

void HaDiscoveryManager::publish_ha_discovery_(mqtt::MQTTClientComponent* mqtt_client)
{
  (void)mqtt_client;
  // HA discovery requires MQTT; without it, mark complete immediately.
  ESP_LOGW(TAG, "HA discovery skipped — no MQTT");
  this->state_ = HA_DISCOVERY_COMPLETE;
}

void HaDiscoveryManager::publish_next_entity_(mqtt::MQTTClientComponent* mqtt_client)
{
  (void)mqtt_client;  /* Used only under USE_ESP_IDF. */
#ifdef USE_ESP_IDF
  if (!this->queue_) return;
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) return;

  HaDiscoveryItem* item = nullptr;
  if (xQueueReceive(this->queue_, &item, 0) == pdTRUE) {
    if (item == nullptr) {
      this->state_ = HA_DISCOVERY_COMPLETE;
      vQueueDelete(this->queue_);
      this->queue_ = nullptr;
      this->task_handle_ = nullptr;
      if (this->task_stack_) { heap_caps_free(this->task_stack_); this->task_stack_ = nullptr; }
      if (this->task_tcb_)   { heap_caps_free(this->task_tcb_);   this->task_tcb_   = nullptr; }
    } else {
      // Use async publish via the adapter if available, otherwise sync fallback
      if (this->mqtt_adapter_ != nullptr) {
        esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
      } else {
        mqtt_client->publish(item->topic, item->payload, 0, true);
      }
      delete item;
    }
  }
#endif
}

std::string HaDiscoveryManager::escape_json_str_(const std::string& s)
{
  std::string out;
  out.reserve(s.size() + 4);
  for (unsigned char c : s) {
    if      (c == '"')  { out += "\\\""; }
    else if (c == '\\') { out += "\\\\"; }
    else if (c < 0x20)  { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c)); out += buf; }
    else                { out += static_cast<char>(c); }
  }
  return out;
}

std::string HaDiscoveryManager::build_device_json_()
{
  std::string j = "{\"identifiers\":[\"" + this->device_id_ + "\"]";
  j += ",\"name\":\"" + this->escape_json_str_(this->device_id_) + "\"";
  j += ",\"manufacturer\":\"GE Appliances\"";
  if (!this->model_number_.empty())
    j += ",\"model\":\"" + this->escape_json_str_(this->model_number_) + "\"";
  if (!this->serial_number_.empty())
    j += ",\"serial_number\":\"" + this->escape_json_str_(this->serial_number_) + "\"";
  j += "}";
  return j;
}

#ifdef USE_ESP_IDF

void HaDiscoveryManager::publish_next_entity_(mqtt::MQTTClientComponent* mqtt_client)
{
  (void)mqtt_client;
  // No-op without MQTT.
}

/*static*/ void HaDiscoveryManager::ha_fetch_task_fn_(void* param)
{
  (void)param;
  // No-op without MQTT.
}

void HaDiscoveryManager::fetch_ha_definitions_()
{
  // No-op without MQTT.
}

bool HaDiscoveryManager::fetch_category_(const std::string& url,
                                          const std::string& device_id,
                                          const std::string& device_json)
{
  (void)url; (void)device_id; (void)device_json;
  return false;
}

bool HaDiscoveryManager::process_jsonl_line_(const std::string& line,
                                              const std::string& device_id,
                                              const std::string& device_json)
{
  (void)line; (void)device_id; (void)device_json;
  return false;
}

#endif

}  // namespace geappliances_bridge
}  // namespace esphome
