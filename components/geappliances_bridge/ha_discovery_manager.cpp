/*!
 * @file
 * @brief HaDiscoveryManager implementation.
 */

#include "ha_discovery_manager.h"
#include "esphome_mqtt_client_adapter.h"
#include "esphome/core/log.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <cstring>

#ifdef USE_ESP_IDF
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
                             bool polling_list_complete,
                             bool subscription_activity_detected,
                             mqtt::MQTTClientComponent* mqtt_client)
{
  if (this->state_ == HA_DISCOVERY_WAITING_FOR_READY) {
    bool ready = false;
    if (is_poll_mode) {
      ready = polling_list_complete;
    } else {
      // Subscription mode: ready once the quiet window elapses after the
      // last new ERD was seen. Do NOT gate on polling_list_complete — the
      // polling bridge is not running in subscription mode.
      if (subscription_activity_detected) {
        if (millis() - this->last_activity_ >= HA_DISCOVERY_QUIET_MS) {
          ready = true;
        }
      }
      // Safety cap: start discovery after 30 s even if activity never
      // settles, so HA discovery is never permanently blocked.
      if (millis() - this->start_time_ >= HA_DISCOVERY_MAX_WAIT_MS) {
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
  if (mqtt_client == nullptr || !mqtt_client->is_connected()) {
    ESP_LOGW(TAG, "MQTT not connected, skipping HA discovery publish");
    return;
  }

#ifdef USE_ESP_IDF
  static constexpr size_t HA_FETCH_MIN_FREE_HEAP = 110 * 1024;
  static constexpr uint32_t HA_FETCH_STACK_SIZE = 49152;

  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (free_heap < HA_FETCH_MIN_FREE_HEAP) {
    ESP_LOGW(TAG, "HA discovery: insufficient free heap, skipping");
    this->state_ = HA_DISCOVERY_COMPLETE;
    return;
  }
  if (largest_block < HA_FETCH_STACK_SIZE) {
    ESP_LOGW(TAG, "HA discovery: heap fragmentation, skipping");
    this->state_ = HA_DISCOVERY_COMPLETE;
    return;
  }

  this->queue_ = xQueueCreate(20, sizeof(HaDiscoveryItem*));
  if (!this->queue_) {
    ESP_LOGE(TAG, "HA discovery: failed to create queue");
    return;
  }

  this->registered_erds_snapshot_ = this->registered_erds_;
  this->state_ = HA_DISCOVERY_PUBLISHING;

  this->task_stack_ = static_cast<StackType_t*>(
    heap_caps_malloc(HA_FETCH_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_INTERNAL));
  this->task_tcb_ = static_cast<StaticTask_t*>(
    heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL));
  if (!this->task_stack_ || !this->task_tcb_) {
    ESP_LOGE(TAG, "HA discovery: failed to allocate task stack/TCB");
    if (this->task_stack_) heap_caps_free(this->task_stack_);
    if (this->task_tcb_)   heap_caps_free(this->task_tcb_);
    this->task_stack_ = nullptr;
    this->task_tcb_   = nullptr;
    vQueueDelete(this->queue_);
    this->queue_ = nullptr;
    this->state_ = HA_DISCOVERY_FAILED;
    return;
  }
  this->task_handle_ = xTaskCreateStatic(
    ha_fetch_task_fn_, "ha_fetch", HA_FETCH_STACK_SIZE, this, 1,
    this->task_stack_, this->task_tcb_);
  if (!this->task_handle_) {
    ESP_LOGE(TAG, "HA discovery: xTaskCreateStatic failed");
    heap_caps_free(this->task_stack_); this->task_stack_ = nullptr;
    heap_caps_free(this->task_tcb_);   this->task_tcb_   = nullptr;
    vQueueDelete(this->queue_);
    this->queue_ = nullptr;
    this->state_ = HA_DISCOVERY_FAILED;
  }
#else
  ESP_LOGW(TAG, "HA discovery requires ESP-IDF framework");
  this->state_ = HA_DISCOVERY_COMPLETE;
#endif
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

/*static*/ void HaDiscoveryManager::ha_fetch_task_fn_(void* param)
{
  auto* self = static_cast<HaDiscoveryManager*>(param);
  self->fetch_ha_definitions_();
  UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
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
  for (uint16_t erd : this->registered_erds_snapshot_) {
    for (int i = 1; i < 10; ++i)
      if (erd >= CATS[i].lo && erd <= CATS[i].hi) { need[i] = true; break; }
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
  ESP_LOGI(TAG, "HA fetch: %s → %d entities", url.c_str(), entities);
  return true;
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
  const char* domain = get_str("d");
  const char* name   = get_str("n");
  const char* role   = get_str("r");
  const char* paired = get_str("p");
  if (!this->registered_erds_snapshot_.empty()) {
    bool registered = this->registered_erds_snapshot_.count(erd_id) > 0;
    if (!registered && role[0] == 'r' && paired[0] != '\0') {
      uint16_t paired_id = static_cast<uint16_t>(strtol(paired, nullptr, 16));
      if (paired_id) registered = this->registered_erds_snapshot_.count(paired_id) > 0 || this->registered_erds_snapshot_.count(erd_id) > 0;
    }
    if (!registered) { cJSON_Delete(root); return false; }
  }
  char erd_id_str[5]; snprintf(erd_id_str, sizeof(erd_id_str), "%04x", erd_id);
  bool is_request = (role[0] == 'r');
  std::string state_topic, command_topic;
  if (is_request && paired[0] != '\0') {
    state_topic = "geappliances/" + device_id + "/erd/0x" + std::string(paired) + "/value";
    command_topic = "geappliances/" + device_id + "/erd/0x" + erd_id_str + "/write";
  } else {
    state_topic = "geappliances/" + device_id + "/erd/0x" + erd_id_str + "/value";
    command_topic = "geappliances/" + device_id + "/erd/0x" + erd_id_str + "/write";
  }
  const char* field_id = get_str("fi");
  std::string unique_id = device_id + "_" + erd_id_str;
  if (field_id[0] != '\0') { unique_id += "_"; unique_id += field_id; }
  const char* vt = get_str("vt"); const char* ct = get_str("ct");
  const char* opts = get_str("o"); const char* unit = get_str("u");
  const char* dc = get_str("dc"); const char* sc = get_str("sc");
  std::string payload;
  auto add_field = [&](const char* key, const char* val) {
    if (val && val[0] != '\0') payload += ",\"" + std::string(key) + "\":\"" + this->escape_json_str_(val) + "\"";
  };
  auto fmt_double = [](double v) -> std::string {
    char buf[32];
    if (v == static_cast<double>(static_cast<long long>(v))) snprintf(buf, sizeof(buf), "%.0f", v);
    else { snprintf(buf, sizeof(buf), "%.6f", v); char* dot = strchr(buf, '.'); if (dot) { char* end = buf + strlen(buf) - 1; while (end > dot && *end == '0') *end-- = '\0'; if (*end == '.') *end = '\0'; } }
    return std::string(buf);
  };
  if (strcmp(domain, "sensor") == 0) {
    payload = "{\"name\":\"" + this->escape_json_str_(name) + "\"";
    payload += ",\"state_topic\":\"" + state_topic + "\",\"unique_id\":\"" + unique_id + "\"";
    add_field("value_template", vt); add_field("unit_of_measurement", unit);
    add_field("device_class", dc); add_field("state_class", sc);
    payload += ",\"device\":" + device_json + "}";
  } else if (strcmp(domain, "binary_sensor") == 0) {
    payload = "{\"name\":\"" + this->escape_json_str_(name) + "\"";
    payload += ",\"state_topic\":\"" + state_topic + "\",\"unique_id\":\"" + unique_id + "\"";
    add_field("value_template", vt); payload += ",\"payload_on\":\"01\",\"payload_off\":\"00\"";
    add_field("device_class", dc); payload += ",\"device\":" + device_json + "}";
  } else if (strcmp(domain, "switch") == 0) {
    payload = "{\"name\":\"" + this->escape_json_str_(name) + "\"";
    payload += ",\"state_topic\":\"" + state_topic + "\",\"command_topic\":\"" + command_topic + "\"";
    payload += ",\"unique_id\":\"" + unique_id + "\""; add_field("value_template", vt);
    payload += ",\"state_on\":\"01\",\"state_off\":\"00\",\"payload_on\":\"01\",\"payload_off\":\"00\"";
    payload += ",\"device\":" + device_json + "}";
  } else if (strcmp(domain, "select") == 0) {
    payload = "{\"name\":\"" + this->escape_json_str_(name) + "\"";
    payload += ",\"state_topic\":\"" + state_topic + "\",\"command_topic\":\"" + command_topic + "\"";
    payload += ",\"unique_id\":\"" + unique_id + "\""; add_field("value_template", vt);
    add_field("command_template", ct);
    if (opts[0] != '\0') payload += ",\"options\":" + std::string(opts);
    payload += ",\"device\":" + device_json + "}";
  } else if (strcmp(domain, "number") == 0) {
    cJSON* dt_item = cJSON_GetObjectItemCaseSensitive(root, "dt");
    cJSON* sf_item = cJSON_GetObjectItemCaseSensitive(root, "sf");
    const char* dtype = (dt_item && cJSON_IsString(dt_item)) ? dt_item->valuestring : "uint8";
    int scale_factor = (sf_item && cJSON_IsNumber(sf_item)) ? static_cast<int>(sf_item->valuedouble) : 1;
    if (scale_factor < 1) scale_factor = 1;
    double type_min, type_max;
    if (strcmp(dtype,"int8")==0){type_min=-128;type_max=127;}else if(strcmp(dtype,"int16")==0){type_min=-32768;type_max=32767;}
    else if(strcmp(dtype,"int24")==0){type_min=-8388608;type_max=8388607;}else if(strcmp(dtype,"int32")==0){type_min=-2147483648.0;type_max=2147483647.0;}
    else if(strcmp(dtype,"uint8")==0){type_min=0;type_max=255;}else if(strcmp(dtype,"uint16")==0){type_min=0;type_max=65535;}
    else if(strcmp(dtype,"uint24")==0){type_min=0;type_max=16777215;}else{type_min=0;type_max=4294967295.0;}
    double min_val=type_min/scale_factor,max_val=type_max/scale_factor,step_val=(scale_factor>1)?(1.0/scale_factor):1.0;
    payload = "{\"name\":\"" + this->escape_json_str_(name) + "\"";
    payload += ",\"state_topic\":\"" + state_topic + "\",\"command_topic\":\"" + command_topic + "\"";
    payload += ",\"unique_id\":\"" + unique_id + "\""; add_field("value_template", vt);
    add_field("command_template", ct); add_field("unit_of_measurement", unit); add_field("device_class", dc);
    payload += ",\"mode\":\"box\",\"min\":" + fmt_double(min_val) + ",\"max\":" + fmt_double(max_val) + ",\"step\":" + fmt_double(step_val);
    payload += ",\"device\":" + device_json + "}";
  } else if (strcmp(domain, "button") == 0) {
    payload = "{\"name\":\"" + this->escape_json_str_(name) + "\"";
    payload += ",\"command_topic\":\"" + command_topic + "\",\"unique_id\":\"" + unique_id + "\"";
    payload += ",\"payload_press\":\"01\""; add_field("device_class", dc);
    payload += ",\"device\":" + device_json + "}";
  } else { cJSON_Delete(root); return false; }
  std::string topic_key = erd_id_str;
  if (field_id[0] != '\0') { topic_key += "_"; topic_key += field_id; }
  std::string topic = "homeassistant/" + std::string(domain) + "/" + device_id + "/" + topic_key + "/config";
  cJSON_Delete(root);
  auto* item = new (std::nothrow) HaDiscoveryItem{std::move(topic), std::move(payload)};
  if (!item) return false;
  return xQueueSend(this->queue_, &item, portMAX_DELAY) == pdTRUE;
}

#endif

}  // namespace geappliances_bridge
}  // namespace esphome
