/*!
 * @file
 * @brief HaDiscoveryManager implementation.
 */

#include "ha_discovery_manager.h"
#include "esphome_mqtt_client_adapter.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>

#ifdef USE_ESP_IDF_STUBS
#  include "esp-idf/esp_http_client.h"
#  include "esp-idf/esp_crt_bundle.h"
#  include "esp-idf/cJSON.h"
#  include "esp-idf/freertos_stub.h"
#  include "esp-idf/esp_heap_caps.h"
#  include "esp-idf/esp_task_wdt.h"
#else
#  include "esp_http_client.h"
#  include "esp_crt_bundle.h"
#  include "cJSON.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "freertos/queue.h"
#  include "esp_heap_caps.h"
#  include "esp_task_wdt.h"
#endif

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "ha_discovery";

bool HaDiscoveryManager::contains_erd_(const tiny_erd_t* erds, uint16_t count, tiny_erd_t target) const
{
  for (uint16_t i = 0; i < count; i++) {
    if (erds[i] == target) return true;
  }
  return false;
}

void HaDiscoveryManager::init(const std::string& base_url,
                              const std::string& device_id,
                              const std::string& model_number,
                              const std::string& serial_number,
                              const tiny_erd_t* registered_erds,
                              uint16_t registered_erds_count,
                              bool generate_device_config)
{
  this->base_url_               = base_url;
  this->device_id_              = device_id;
  this->model_number_           = model_number;
  this->serial_number_          = serial_number;
  uint16_t n = (registered_erds_count > HA_DISCOVERY_MAX_ERDS) ? HA_DISCOVERY_MAX_ERDS : registered_erds_count;
  for (uint16_t i = 0; i < n; i++) {
    this->registered_erds_[i] = registered_erds[i];
    this->registered_erds_snapshot_[i] = registered_erds[i];
  }
  this->registered_erds_count_ = n;
  this->registered_erds_snapshot_count_ = n;
  this->seen_erds_count_ = 0;
  this->generate_device_config_ = generate_device_config;
  this->state_                  = HA_DISCOVERY_WAITING_FOR_READY;
  this->last_activity_          = millis();
  this->start_time_             = millis();
}

void HaDiscoveryManager::set_registered_erds(const tiny_erd_t* erds, uint16_t count)
{
  uint16_t n = (count > HA_DISCOVERY_MAX_ERDS) ? HA_DISCOVERY_MAX_ERDS : count;
  for (uint16_t i = 0; i < n; i++) {
    this->registered_erds_[i] = erds[i];
    this->registered_erds_snapshot_[i] = erds[i];
  }
  this->registered_erds_count_ = n;
  this->registered_erds_snapshot_count_ = n;
}

void HaDiscoveryManager::on_erd_seen(tiny_erd_t erd)
{
  if (this->state_ != HA_DISCOVERY_WAITING_FOR_READY) return;
  if (!contains_erd_(this->seen_erds_, this->seen_erds_count_, erd)) {
    if (this->seen_erds_count_ < HA_DISCOVERY_MAX_ERDS) {
      this->seen_erds_[this->seen_erds_count_++] = erd;
    }
    this->last_activity_ = millis();
  }
}

void HaDiscoveryManager::set_mqtt_adapter(esphome_mqtt_client_adapter_t* mqtt_adapter)
{
  this->mqtt_adapter_ = mqtt_adapter;
}

void HaDiscoveryManager::cleanup()
{
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
}

void HaDiscoveryManager::run(bool device_steady_state)
{
  if (this->state_ == HA_DISCOVERY_WAITING_FOR_READY) {
    if (device_steady_state) {
      this->publish_ha_discovery_();
    }
  }

  if (this->state_ == HA_DISCOVERY_PUBLISHING) {
    uint32_t now = millis();
    if (now - this->last_publish_ms_ >= HA_ENTITY_PUBLISH_INTERVAL_MS) {
      this->last_publish_ms_ = now;
      this->publish_next_entity_();
    }
  }

  if (this->state_ == HA_DISCOVERY_CLEARING) {
    uint32_t now = millis();
    if (now - this->last_publish_ms_ >= HA_ENTITY_PUBLISH_INTERVAL_MS) {
      this->last_publish_ms_ = now;
      this->publish_next_clear_();
    }
  }
}

void HaDiscoveryManager::clear_ha_discovery_sync()
{
  if (this->published_topics_count_ == 0) {
    return;
  }
  ESP_LOGI(TAG, "Clearing %u HA discovery topics (sync)", this->published_topics_count_);
  for (uint16_t i = 0; i < this->published_topics_count_; i++) {
    const auto& t = this->published_topics_[i];
    std::string topic = "homeassistant/" + t.component + "/" + this->device_id_ + "/" + t.erd_hex + "/config";
    if (this->mqtt_adapter_) {
      esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, topic, "", true);
    }
  }
  this->published_topics_count_ = 0;
  this->clear_index_ = 0;
  this->state_ = HA_DISCOVERY_IDLE;
}

void HaDiscoveryManager::clear_ha_discovery()
{
  if (this->published_topics_count_ == 0) {
    ESP_LOGW(TAG, "No published topics to clear");
    return;
  }
  ESP_LOGI(TAG, "Clearing %u HA discovery topics", this->published_topics_count_);
  this->clear_index_ = 0;
  this->last_publish_ms_ = millis();
  this->state_ = HA_DISCOVERY_CLEARING;
}

void HaDiscoveryManager::publish_next_clear_()
{
  if (this->clear_index_ >= this->published_topics_count_) {
    ESP_LOGI(TAG, "Cleared all HA discovery topics");
    this->state_ = HA_DISCOVERY_COMPLETE;
    return;
  }
  const auto& t = this->published_topics_[this->clear_index_++];
  std::string topic = "homeassistant/" + t.component + "/" + this->device_id_ + "/" + t.erd_hex + "/config";
  if (this->mqtt_adapter_) {
    esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, topic, "", true);
  }
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
  // Build JSON into a fixed buffer to avoid std::string concatenation
  // creating temporary heap allocations.  Max device_id is ~64 chars,
  // model/serial ~32 each; this buffer is more than enough.
  char buf[512];
  int pos = snprintf(buf, sizeof(buf),
    "{\"identifiers\":[\"%s\"],\"name\":\"", this->device_id_.c_str());

  // Escape and append name (device_id).
  for (unsigned char c : this->device_id_) {
    if (pos >= (int)sizeof(buf) - 8) break;
    if      (c == '"')  { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\\""); }
    else if (c == '\\') { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\\\"); }
    else if (c < 0x20)  { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\u%04x", c); }
    else                { buf[pos++] = (char)c; }
  }
  if (pos < (int)sizeof(buf) - 64) {
    pos += snprintf(buf + pos, sizeof(buf) - pos,
      "\",\"manufacturer\":\"GE Appliances\"");
  }
  if (!this->model_number_.empty() && pos < (int)sizeof(buf) - 128) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"model\":\"");
    for (unsigned char c : this->model_number_) {
      if (pos >= (int)sizeof(buf) - 8) break;
      if      (c == '"')  { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\\""); }
      else if (c == '\\') { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\\\"); }
      else if (c < 0x20)  { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\u%04x", c); }
      else                { buf[pos++] = (char)c; }
    }
    if (pos < (int)sizeof(buf) - 2) buf[pos++] = '"';
  }
  if (!this->serial_number_.empty() && pos < (int)sizeof(buf) - 128) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"serial_number\":\"");
    for (unsigned char c : this->serial_number_) {
      if (pos >= (int)sizeof(buf) - 8) break;
      if      (c == '"')  { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\\""); }
      else if (c == '\\') { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\\\"); }
      else if (c < 0x20)  { pos += snprintf(buf + pos, sizeof(buf) - pos, "\\u%04x", c); }
      else                { buf[pos++] = (char)c; }
    }
    if (pos < (int)sizeof(buf) - 2) buf[pos++] = '"';
  }
  if (pos < (int)sizeof(buf) - 2) buf[pos++] = '}';
  buf[pos] = '\0';
  return std::string(buf);
}

#ifndef USE_ESP_IDF
#error "generate_device_config requires the ESP-IDF framework. Please set framework: type: esp-idf or set generate_device_config: false"
#endif

#ifdef USE_ESP_IDF_STUBS
// ESP-IDF stubs: no real FreeRTOS or HTTP client in test builds.
void HaDiscoveryManager::publish_ha_discovery_()
{
  ESP_LOGD(TAG, "HA discovery triggered (ESP-IDF stubs — skipping entity publish)");
  this->state_ = HA_DISCOVERY_COMPLETE;
}

void HaDiscoveryManager::publish_next_entity_()
{
  // No MQTT broker — no-op.
}
#else  /* !USE_ESP_IDF_STUBS — real ESP-IDF implementation */

/*static*/ void HaDiscoveryManager::ha_fetch_task_fn_(void* param)
{
  auto* self = static_cast<HaDiscoveryManager*>(param);
  self->fetch_ha_definitions_();
  UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
  (void)hwm;
  ESP_LOGI(TAG, "ha_fetch: done — stack HWM %u B", static_cast<unsigned>(hwm));
  HaDiscoveryItem* sentinel = nullptr;
  xQueueSend(self->queue_, &sentinel, portMAX_DELAY);
  vTaskDelete(nullptr);
}

void HaDiscoveryManager::fetch_ha_definitions_()
{
  struct Category { const char* name; uint16_t lo; uint16_t hi; };
  static const Category CATS[] = {
    {"common",0x0000,0x0FFF},{"refrigeration",0x1000,0x1FFF},{"laundry",0x2000,0x2FFF},
    {"dishwasher",0x3000,0x3FFF},{"waterheater",0x4000,0x4FFF},{"range",0x5000,0x5FFF},
    {"airconditioning",0x7000,0x7FFF},{"waterfilter",0x8000,0x8FFF},
    {"smallappliance",0x9000,0x9FFF},{"energy",0xD000,0xDFFF},
  };
  bool need[10] = {};
  need[0] = true;
  for (uint16_t i = 0; i < this->registered_erds_snapshot_count_; i++) {
    uint16_t erd = this->registered_erds_snapshot_[i];
    for (int j = 1; j < 10; ++j)
      if (erd >= CATS[j].lo && erd <= CATS[j].hi) { need[j] = true; break; }
  }
  std::string device_json = this->build_device_json_();
  for (int i = 0; i < 10; ++i) {
    if (!need[i]) continue;
    std::string url = this->base_url_ + "/" + CATS[i].name + ".jsonl";
    this->fetch_category_(url, this->device_id_, device_json);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

bool HaDiscoveryManager::fetch_category_(const std::string& url,
                                          const std::string& device_id,
                                          const std::string& device_json)
{
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 20000;
  cfg.max_redirection_count = 5;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;
  if (esp_http_client_open(client, 0) != ESP_OK) { esp_http_client_cleanup(client); return false; }
  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status == 404) { esp_http_client_cleanup(client); return true; }
  if (status != 200) { esp_http_client_cleanup(client); return false; }

  static constexpr int READ_BUF = 512;
  static constexpr int LINE_BUF = 8192;
  char* read_buf = static_cast<char*>(malloc(READ_BUF));
  char* line_buf = static_cast<char*>(malloc(LINE_BUF));
  if (!read_buf || !line_buf) { free(read_buf); free(line_buf); esp_http_client_cleanup(client); return false; }

  int line_pos = 0; int entities = 0; int read_len;
  while ((read_len = esp_http_client_read(client, read_buf, READ_BUF - 1)) > 0) {
    for (int i = 0; i < read_len; ++i) {
      char c = read_buf[i];
      if (c == '\n' || c == '\r') {
        if (line_pos > 2) { line_buf[line_pos] = '\0'; if (this->process_jsonl_line_(line_buf, device_id, device_json)) ++entities; }
        line_pos = 0;
      } else if (line_pos < LINE_BUF - 1) { line_buf[line_pos++] = c; }
    }
  }
  if (line_pos > 2) { line_buf[line_pos] = '\0'; if (this->process_jsonl_line_(line_buf, device_id, device_json)) ++entities; }
  free(read_buf); free(line_buf); esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "HA fetch: %s -> %d entities", url.c_str(), entities);
  return true;
}

void HaDiscoveryManager::publish_ha_discovery_()
{
  // Spawn a FreeRTOS task to fetch JSONL definitions and queue entities.
  static constexpr int STACK_SIZE = 48 * 1024;  // 48 KB
  this->task_stack_ = static_cast<StackType_t*>(heap_caps_malloc(STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_8BIT));
  this->task_tcb_ = static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_8BIT));
  if (!this->task_stack_ || !this->task_tcb_) {
    free(this->task_stack_); free(this->task_tcb_);
    this->task_stack_ = nullptr; this->task_tcb_ = nullptr;
    ESP_LOGE(TAG, "Failed to allocate stack/TCB for HA discovery task");
    this->state_ = HA_DISCOVERY_FAILED;
    return;
  }
  this->queue_ = xQueueCreateStatic(64, sizeof(HaDiscoveryItem*),
      static_cast<uint8_t*>(heap_caps_malloc(64 * sizeof(HaDiscoveryItem*), MALLOC_CAP_8BIT)),
      static_cast<StaticQueue_t*>(heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_8BIT)));
  if (!this->queue_) {
    free(this->task_stack_); free(this->task_tcb_);
    this->task_stack_ = nullptr; this->task_tcb_ = nullptr;
    ESP_LOGE(TAG, "Failed to create queue for HA discovery task");
    this->state_ = HA_DISCOVERY_FAILED;
    return;
  }
  this->task_handle_ = xTaskCreateStatic(ha_fetch_task_fn_, "ha_fetch", STACK_SIZE, this, 1,
      this->task_stack_, this->task_tcb_);
  if (!this->task_handle_) {
    vQueueDelete(this->queue_); this->queue_ = nullptr;
    free(this->task_stack_); free(this->task_tcb_);
    this->task_stack_ = nullptr; this->task_tcb_ = nullptr;
    ESP_LOGE(TAG, "Failed to create HA discovery task");
    this->state_ = HA_DISCOVERY_FAILED;
    return;
  }
  this->state_ = HA_DISCOVERY_PUBLISHING;
}

void HaDiscoveryManager::publish_next_entity_()
{
  HaDiscoveryItem* item = nullptr;
  if (xQueueReceive(this->queue_, &item, 0) == pdTRUE) {
    if (this->mqtt_adapter_) {
      esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
    }
    delete item;
  } else {
    // Queue empty — fetch task is done or not yet produced items.
    // Check if the fetch task has terminated.
    if (this->task_handle_ == nullptr) {
      // Fetch task completed. Check for any remaining items.
      while (xQueueReceive(this->queue_, &item, 0) == pdTRUE) {
        if (this->mqtt_adapter_) {
          esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
        }
        delete item;
      }
      vQueueDelete(this->queue_); this->queue_ = nullptr;
      ESP_LOGI(TAG, "HA discovery complete — all entities published");
      this->state_ = HA_DISCOVERY_COMPLETE;
    }
  }
}

bool HaDiscoveryManager::process_jsonl_line_(const std::string& line,
                                              const std::string& device_id,
                                              const std::string& device_json)
{
  cJSON* root = cJSON_Parse(line.c_str());
  if (!root) return false;
  auto get_str = [&](const char* key) -> const char* {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
  };
  const char* erd_hex = get_str("i");
  if (erd_hex[0] == '\0') { cJSON_Delete(root); return false; }
  uint16_t erd_id = static_cast<uint16_t>(strtol(erd_hex, nullptr, 16));

  const char* role = get_str("r");
  const char* paired = get_str("p");
  if (!this->registered_erds_snapshot_count_) {
    /* No registered ERD filter — accept all. */
  } else {
    bool registered = this->contains_erd_(this->registered_erds_snapshot_, this->registered_erds_snapshot_count_, erd_id);
    if (!registered && role[0] == 'r' && paired[0] != '\0') {
      uint16_t paired_id = static_cast<uint16_t>(strtol(paired, nullptr, 16));
      if (paired_id) registered = this->contains_erd_(this->registered_erds_snapshot_, this->registered_erds_snapshot_count_, paired_id) ||
                                  this->contains_erd_(this->registered_erds_snapshot_, this->registered_erds_snapshot_count_, erd_id);
    }
    if (!registered) { cJSON_Delete(root); return false; }
  }

  /* Build the MQTT discovery payload. */
  std::string payload = "{\"device\":" + device_json;
  payload += ",\"name\":\"" + this->escape_json_str_(get_str("n")) + "\"";
  payload += ",\"unique_id\":\"" + device_id + "_" + std::string(erd_hex) + "\"";
  payload += ",\"object_id\":\"" + this->escape_json_str_(get_str("o")) + "\"";

  const char* unit = get_str("u");
  if (unit[0] != '\0') {
    payload += ",\"unit_of_measurement\":\"" + this->escape_json_str_(unit) + "\"";
  }

  const char* ic = get_str("ic");
  if (ic[0] != '\0') {
    payload += ",\"icon\":\"" + this->escape_json_str_(ic) + "\"";
  }

  const char* dev_cl = get_str("d");
  if (dev_cl[0] != '\0') {
    payload += ",\"device_class\":\"" + this->escape_json_str_(dev_cl) + "\"";
  }

  const char* ent_cat = get_str("e");
  if (ent_cat[0] != '\0') {
    payload += ",\"entity_category\":\"" + this->escape_json_str_(ent_cat) + "\"";
  }

  const char* state_topic = get_str("s");
  if (state_topic[0] != '\0') {
    payload += ",\"state_topic\":\"" + this->escape_json_str_(state_topic) + "\"";
  }

  const char* cmd_topic = get_str("c");
  if (cmd_topic[0] != '\0') {
    payload += ",\"command_topic\":\"" + this->escape_json_str_(cmd_topic) + "\"";
  }

  const char* payload_on = get_str("on");
  if (payload_on[0] != '\0') {
    payload += ",\"payload_on\":\"" + this->escape_json_str_(payload_on) + "\"";
  }

  const char* payload_off = get_str("of");
  if (payload_off[0] != '\0') {
    payload += ",\"payload_off\":\"" + this->escape_json_str_(payload_off) + "\"";
  }

  const char* avail_topic = get_str("a");
  if (avail_topic[0] != '\0') {
    payload += ",\"availability_topic\":\"" + this->escape_json_str_(avail_topic) + "\"";
  }

  const char* json_attr = get_str("j");
  if (json_attr[0] != '\0') {
    payload += ",\"json_attributes_topic\":\"" + this->escape_json_str_(json_attr) + "\"";
  }

  const char* val_tpl = get_str("v");
  if (val_tpl[0] != '\0') {
    payload += ",\"value_template\":\"" + this->escape_json_str_(val_tpl) + "\"";
  }

  const char* cmd_tpl = get_str("cm");
  if (cmd_tpl[0] != '\0') {
    payload += ",\"command_template\":\"" + this->escape_json_str_(cmd_tpl) + "\"";
  }

  const char* opt = get_str("opt");
  if (opt[0] != '\0') {
    payload += ",\"options\":[";
    bool first = true;
    for (const char* p = opt; *p; ) {
      const char* comma = strchr(p, ',');
      std::string val;
      if (comma) {
        val = std::string(p, comma - p);
        p = comma + 1;
      } else {
        val = p;
        p += val.size();
      }
      if (!first) payload += ",";
      payload += "\"" + this->escape_json_str_(val) + "\"";
      first = false;
    }
    payload += "]";
  }

  payload += "}";

  const char* comp = get_str("t");
  if (comp[0] == '\0') { cJSON_Delete(root); return false; }

  // Track this topic for later clearing
  if (this->published_topics_count_ < HA_DISCOVERY_MAX_PUBLISHED_TOPICS) {
    this->published_topics_[this->published_topics_count_].component = std::string(comp);
    this->published_topics_[this->published_topics_count_].erd_hex = std::string(erd_hex);
    this->published_topics_count_++;
  }

  std::string topic = "homeassistant/" + std::string(comp) + "/" + device_id + "/" + std::string(erd_hex) + "/config";

  auto* item = new HaDiscoveryItem();
  item->topic = std::move(topic);
  item->payload = std::move(payload);

  if (this->mqtt_adapter_) {
    /* Publish synchronously through the adapter. */
    esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
    delete item;
  } else {
    /* Queue for async publishing. */
    if (this->queue_) {
      xQueueSend(this->queue_, &item, 0);
    } else {
      delete item;
    }
  }

  cJSON_Delete(root);
  return true;
}

#endif  /* USE_ESP_IDF_STUBS */

}  // namespace geappliances_bridge
}  // namespace esphome
