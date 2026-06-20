/*!
 * @file
 * @brief HaDiscoveryManager implementation.
 */

#include "ha_discovery_manager.h"
#include "ha_discovery_data.h"
#include "esphome_mqtt_client_adapter.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>

#ifdef USE_ESP_IDF_STUBS
#  include "esp-idf/cJSON.h"
#  include "esp-idf/freertos_stub.h"
#  include "esp-idf/esp_heap_caps.h"
#  include "esp-idf/esp_task_wdt.h"
#  include "esp-idf/esp_zlib_stub.h"
#else
#  include "cJSON.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "freertos/queue.h"
#  include "esp_heap_caps.h"
#  include "esp_task_wdt.h"
#  include "miniz.h"
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

void HaDiscoveryManager::init(const std::string& device_id,
                              const std::string& model_number,
                              const std::string& serial_number,
                              erd_cache_t* erd_cache,
                              bool generate_device_config)
{
  this->device_id_              = device_id;
  this->model_number_           = model_number;
  this->serial_number_          = serial_number;
  this->erd_cache_              = erd_cache;
  this->seen_erds_count_        = 0;
  this->generate_device_config_ = generate_device_config;
  this->state_                  = HA_DISCOVERY_WAITING_FOR_READY;
  this->last_activity_          = millis();
  this->start_time_             = millis();
}

void HaDiscoveryManager::set_registered_erds(const tiny_erd_t* erds, uint16_t count)
{
  // No-op: the fetch task reads the ERD cache directly.
  // Kept for API compatibility with callers that may still invoke it.
  (void)erds;
  (void)count;
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

int HaDiscoveryManager::escape_json_str_(const char* s, char* buf, int buf_size)
{
  int pos = 0;
  for (const char* p = s; *p && pos < buf_size - 1; p++) {
    unsigned char c = static_cast<unsigned char>(*p);
    int needed;
    if (c == '"') {
      needed = snprintf(buf + pos, buf_size - pos, "\\\"");
    } else if (c == '\\') {
      needed = snprintf(buf + pos, buf_size - pos, "\\\\");
    } else if (c < 0x20) {
      needed = snprintf(buf + pos, buf_size - pos, "\\u%04x", c);
    } else {
      buf[pos++] = static_cast<char>(c);
      needed = 1;
    }
    pos += needed;
  }
  if (pos < buf_size) buf[pos] = '\0';
  return pos;
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
  // Send sentinel to signal completion to the main loop.
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
  // Iterate the ERD cache directly to determine which categories are needed.
  uint16_t iterator = 0;
  while (true) {
    erd_cache_entry_t* entry = erd_cache_get_next_entry(this->erd_cache_, &iterator);
    if (!entry) break;
    for (int j = 1; j < 10; ++j)
      if (entry->erd >= CATS[j].lo && entry->erd <= CATS[j].hi) { need[j] = true; break; }
  }

  std::string device_json = this->build_device_json_();

  // For each needed category, find the matching embedded data and process it.
  for (int i = 0; i < 10; ++i) {
    if (!need[i]) continue;

    // Find matching category in embedded data
    const HaDiscoveryCategory* cat = nullptr;
    for (uint16_t c = 0; c < ha_discovery_category_count; c++) {
      if (strcmp(ha_discovery_categories[c].name, CATS[i].name) == 0) {
        cat = &ha_discovery_categories[c];
        break;
      }
    }
    if (!cat) continue;

    this->process_category_(cat, this->device_id_, device_json);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
bool HaDiscoveryManager::process_category_(const HaDiscoveryCategory* cat,
                                           const std::string& device_id,
                                           const std::string& device_json)
{
#ifdef USE_ESP_IDF_STUBS
  ESP_LOGE(TAG, "HA fetch: decompression not available in stub build");
  return false;
#else
  // Each category is split into independently-compressible chunks.
  // Decompress each chunk into a single small buffer, process lines,
  // then move to the next chunk — same buffer reused throughout.
  // Buffer size = max decompressed chunk size (typically 2048 bytes).
  uint8_t* out_buf = static_cast<uint8_t*>(malloc(cat->max_decompressed_chunk));
  if (!out_buf) {
    ESP_LOGE(TAG, "HA fetch: failed to allocate %u bytes for decompression buffer",
             static_cast<unsigned>(cat->max_decompressed_chunk));
    return false;
  }

  char line_buf[4096];
  int line_pos = 0;
  int entities = 0;

  for (uint16_t ci = 0; ci < cat->num_chunks; ci++) {
    const HaDiscoveryChunk* chunk = &cat->chunks[ci];
    const uint8_t* src = cat->data + chunk->offset;
    size_t src_size = chunk->size;
    size_t dst_size = cat->max_decompressed_chunk;

    tinfl_decompressor decomp;
    tinfl_init(&decomp);

    tinfl_status status = tinfl_decompress(
        &decomp, src, &src_size,
        out_buf, out_buf, &dst_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if (status != TINFL_STATUS_DONE) {
      ESP_LOGE(TAG, "HA fetch: chunk %u of %s failed (status %d)", ci, cat->name, status);
      free(out_buf);
      return false;
    }

    // Process decompressed bytes as lines.
    for (size_t i = 0; i < dst_size; i++) {
      char ch = static_cast<char>(out_buf[i]);
      if (ch == '\n' || ch == '\r') {
        if (line_pos > 2) {
          line_buf[line_pos] = '\0';
          if (process_jsonl_line_(line_buf, device_id, device_json)) {
            entities++;
          }
        }
        line_pos = 0;
      } else if (line_pos < (int)sizeof(line_buf) - 1) {
        line_buf[line_pos++] = ch;
      }
    }
  }

  // Process any remaining data in line buffer.
  if (line_pos > 2) {
    line_buf[line_pos] = '\0';
    if (process_jsonl_line_(line_buf, device_id, device_json)) {
      entities++;
    }
  }

  free(out_buf);
  ESP_LOGI(TAG, "HA fetch: %s -> %d entities", cat->name, entities);
  return true;
#endif
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
    if (item == nullptr) {
      // Sentinel — fetch task is done.
      vQueueDelete(this->queue_); this->queue_ = nullptr;
      // Clean up task resources.
      free(this->task_stack_); free(this->task_tcb_);
      this->task_stack_ = nullptr; this->task_tcb_ = nullptr;
      this->task_handle_ = nullptr;
      ESP_LOGI(TAG, "HA discovery complete — %u entities published",
               static_cast<unsigned>(this->published_topics_count_));
      this->state_ = HA_DISCOVERY_COMPLETE;
      return;
    }
    if (this->mqtt_adapter_) {
      esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
    }
    delete item;
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

  // Check if this ERD exists in the cache.
  if (this->erd_cache_ != nullptr) {
    bool found = false;
    uint16_t iterator = 0;
    while (true) {
      erd_cache_entry_t* entry = erd_cache_get_next_entry(this->erd_cache_, &iterator);
      if (!entry) break;
      if (entry->erd == erd_id) { found = true; break; }
    }
    // Check paired ERD if not found and this is a request role with a paired ERD.
    if (!found && role[0] == 'r' && paired[0] != '\0') {
      uint16_t paired_id = static_cast<uint16_t>(strtol(paired, nullptr, 16));
      if (paired_id) {
        iterator = 0;
        while (!found) {
          erd_cache_entry_t* entry = erd_cache_get_next_entry(this->erd_cache_, &iterator);
          if (!entry) break;
          if (entry->erd == paired_id || entry->erd == erd_id) { found = true; break; }
        }
      }
    }
    if (!found) { cJSON_Delete(root); return false; }
  }

  const char* fi = get_str("fi");
  char unique_suffix[64];
  if (fi[0] != '\0') {
    snprintf(unique_suffix, sizeof(unique_suffix), "%s_%s", erd_hex, fi);
  } else {
    snprintf(unique_suffix, sizeof(unique_suffix), "%s", erd_hex);
  }

  const char* comp = get_str("d");
  if (comp[0] == '\0') { cJSON_Delete(root); return false; }

  // Build payload on a stack buffer — no heap allocation.
  char payload_buf[1024];
  char esc_buf[128];
  int pos = 0;
  auto esc = [&](const char* s) -> const char* {
    escape_json_str_(s, esc_buf, sizeof(esc_buf));
    return esc_buf;
  };
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      "{\"device\":%s", device_json.c_str());
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      ",\"name\":\"%s\"", esc(get_str("n")));
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      ",\"unique_id\":\"%s_%s\"", device_id.c_str(), unique_suffix);
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      ",\"object_id\":\"%s\"", esc(get_str("o")));

  const char* unit = get_str("u");
  if (unit[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"unit_of_measurement\":\"%s\"", esc(unit));
  }
  const char* ic = get_str("ic");
  if (ic[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"icon\":\"%s\"", esc(ic));
  }
  const char* dev_cl = get_str("dc");
  if (dev_cl[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"device_class\":\"%s\"", esc(dev_cl));
  }
  const char* ent_cat = get_str("e");
  if (ent_cat[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"entity_category\":\"%s\"", esc(ent_cat));
  }
  const char* state_topic = get_str("s");
  if (state_topic[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"state_topic\":\"%s\"", esc(state_topic));
  }
  const char* cmd_topic = get_str("c");
  if (cmd_topic[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"command_topic\":\"%s\"", esc(cmd_topic));
  }
  const char* payload_on = get_str("on");
  if (payload_on[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"payload_on\":\"%s\"", esc(payload_on));
  }
  const char* payload_off = get_str("of");
  if (payload_off[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"payload_off\":\"%s\"", esc(payload_off));
  }
  const char* avail_topic = get_str("a");
  if (avail_topic[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"availability_topic\":\"%s\"", esc(avail_topic));
  }
  const char* json_attr = get_str("j");
  if (json_attr[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"json_attributes_topic\":\"%s\"", esc(json_attr));
  }
  const char* val_tpl = get_str("v");
  if (val_tpl[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"value_template\":\"%s\"", esc(val_tpl));
  }
  const char* cmd_tpl = get_str("cm");
  if (cmd_tpl[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"command_template\":\"%s\"", esc(cmd_tpl));
  }
  const char* opt = get_str("opt");
  if (opt[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, ",\"options\":[");
    bool first = true;
    for (const char* p = opt; *p; ) {
      const char* comma = strchr(p, ',');
      char val_buf[64];
      int vlen;
      if (comma) {
        vlen = (int)(comma - p);
        if (vlen >= (int)sizeof(val_buf)) vlen = (int)sizeof(val_buf) - 1;
      } else {
        vlen = (int)strlen(p);
        if (vlen >= (int)sizeof(val_buf)) vlen = (int)sizeof(val_buf) - 1;
        p += vlen;
      }
      memcpy(val_buf, p, vlen);
      val_buf[vlen] = '\0';
      p = comma ? comma + 1 : p + vlen;
      if (!first) pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, ",");
      pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, "\"%s\"", esc(val_buf));
      first = false;
    }
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, "]");
  }
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, "}");

  if (pos >= (int)sizeof(payload_buf)) {
    ESP_LOGW(TAG, "HA fetch: payload too large for ERD %s", erd_hex);
    cJSON_Delete(root);
    return false;
  }

  // Build topic on stack.
  char topic_buf[128];
  snprintf(topic_buf, sizeof(topic_buf),
      "homeassistant/%s/%s/%s/config", comp, device_id.c_str(), unique_suffix);

  // Track this topic for later clearing
  if (this->published_topics_count_ < HA_DISCOVERY_MAX_PUBLISHED_TOPICS) {
    this->published_topics_[this->published_topics_count_].component = std::string(comp);
    this->published_topics_[this->published_topics_count_].erd_hex = std::string(unique_suffix);
    this->published_topics_count_++;
  }

  // Queue the item for the main loop to publish.
  // Blocks if queue is full — backpressures the fetch task.
  auto* item = new HaDiscoveryItem();
  item->topic = topic_buf;
  item->payload = payload_buf;

  if (this->queue_) {
    // Wait up to 2s for queue space — the main loop drains at 50ms/entity.
    // With 64 slots that's 3.2s to fully drain, but we don't want to block
    // the fetch task indefinitely if the main loop is stalled.
    if (xQueueSend(this->queue_, &item, pdMS_TO_TICKS(2000)) != pdTRUE) {
      ESP_LOGW(TAG, "HA fetch: queue full after 2s, dropping entity for ERD %s", erd_hex);
      delete item;
    }
  } else {
    delete item;
  }

  cJSON_Delete(root);
  return true;
}

#endif  /* USE_ESP_IDF_STUBS */

}  // namespace geappliances_bridge
}  // namespace esphome
