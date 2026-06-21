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
#  include "esp_heap_caps.h"
#  include "esp_task_wdt.h"
#  include "miniz.h"
#endif

namespace esphome {
namespace geappliances_bridge {

static const char* const TAG __attribute__((unused)) = "ha_discovery";
static constexpr uint32_t HA_STALE_DISCOVERY_TIMEOUT_MS = 2000;

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
  // Idempotent: if publish_next_entity_() already cleaned up after the
  // sentinel, all pointers are null and this is a no-op.
  if (this->queue_ == nullptr && this->task_stack_ == nullptr) {
    return;
  }

  // If a fetch task is still running, signal it to stop via the sentinel.
  HaDiscoveryItem* sentinel = nullptr;
  xQueueSend(this->queue_, &sentinel, 0);

  // Drain all remaining items from the queue.
  {
    HaDiscoveryItem* item = nullptr;
    while (xQueueReceive(this->queue_, &item, 0) == pdTRUE) {
      if (item != nullptr) delete item;
    }
  }
  // Re-send the sentinel now that space is guaranteed.
  xQueueSend(this->queue_, &sentinel, 0);

  // Forcefully delete the task if it hasn't terminated.
  if (this->task_handle_ != nullptr) {
    vTaskDelete(this->task_handle_);
    this->task_handle_ = nullptr;
  }

  // Delete the queue before freeing stack/TCB (queue may still be referenced).
  vQueueDelete(this->queue_);
  this->queue_ = nullptr;

  // Free heap-allocated stack and TCB.
  if (this->task_stack_ != nullptr) {
    free(this->task_stack_);
    this->task_stack_ = nullptr;
  }
  if (this->task_tcb_ != nullptr) {
    free(this->task_tcb_);
    this->task_tcb_ = nullptr;
  }
}

void HaDiscoveryManager::run(bool device_steady_state)
{
  if (this->state_ == HA_DISCOVERY_WAITING_FOR_READY) {
    // Safety cap: if steady state is not reached within the max wait time,
    // fail rather than waiting forever (e.g., subscription bridge stuck).
    if (millis() - this->start_time_ > HA_DISCOVERY_MAX_WAIT_MS) {
      ESP_LOGW(TAG, "HA discovery timed out after %u ms — marking failed",
               static_cast<unsigned>(HA_DISCOVERY_MAX_WAIT_MS));
      this->state_ = HA_DISCOVERY_FAILED;
      return;
    }
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

  if (this->state_ == HA_DISCOVERY_CLEANING_STALE) {
    uint32_t now = millis();
    if (now - this->stale_discovery_start_ms_ >= HA_STALE_DISCOVERY_TIMEOUT_MS) {
      // Timeout reached — unsubscribe and start cleanup
      if (this->mqtt_adapter_ && this->stale_subscription_handle_) {
        esphome_mqtt_client_adapter_unsubscribe(this->mqtt_adapter_, this->stale_subscription_handle_);
        this->stale_subscription_handle_ = 0;
      }
      ESP_LOGI(TAG, "Stale discovery timeout — found %u stale topics",
               static_cast<unsigned>(this->stale_topics_count_));
      this->stale_cleanup_index_ = 0;
      this->last_publish_ms_ = now;
    }
    if (!this->stale_subscription_handle_) {
      // Subscription closed — process stale topics at rate limit
      if (now - this->last_publish_ms_ >= HA_ENTITY_PUBLISH_INTERVAL_MS) {
        this->last_publish_ms_ = now;
        this->publish_stale_cleanup_();
      }
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
    char topic_buf[128];
    snprintf(topic_buf, sizeof(topic_buf),
        "homeassistant/%s/%s/%s/config", t.component, this->device_id_.c_str(), t.erd_hex);
    if (this->mqtt_adapter_) {
      esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, topic_buf, "", true);
    }
    // Feed the task watchdog — with many topics, this loop can run for seconds.
#ifdef USE_ESP32
    esp_task_wdt_reset();
#endif
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
  char topic_buf[128];
  snprintf(topic_buf, sizeof(topic_buf),
      "homeassistant/%s/%s/%s/config", t.component, this->device_id_.c_str(), t.erd_hex);
  if (this->mqtt_adapter_) {
    esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, topic_buf, "", true);
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
    } else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) {
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

const char* HaDiscoveryManager::build_device_json_()
{
  // Build JSON into the pre-allocated member buffer to avoid heap allocation.
  char* buf = this->device_json_buf_;
  int buf_size = static_cast<int>(sizeof(this->device_json_buf_));
  int pos = snprintf(buf, buf_size,
    "{\"identifiers\":[\"%s\"],\"name\":\"", this->device_id_.c_str());

  // Escape and append name (device_id).
  for (unsigned char c : this->device_id_) {
    if (pos >= buf_size - 8) break;
    if      (c == '"')  { pos += snprintf(buf + pos, buf_size - pos, "\\\""); }
    else if (c == '\\') { pos += snprintf(buf + pos, buf_size - pos, "\\\\"); }
    else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F))  { pos += snprintf(buf + pos, buf_size - pos, "\\u%04x", c); }
    else                { buf[pos++] = (char)c; }
  }
  if (pos < buf_size - 64) {
    pos += snprintf(buf + pos, buf_size - pos,
      "\",\"manufacturer\":\"GE Appliances\"");
  }
  if (!this->model_number_.empty() && pos < buf_size - 128) {
    pos += snprintf(buf + pos, buf_size - pos, ",\"model\":\"");
    for (unsigned char c : this->model_number_) {
      if (pos >= buf_size - 8) break;
      if      (c == '"')  { pos += snprintf(buf + pos, buf_size - pos, "\\\""); }
      else if (c == '\\') { pos += snprintf(buf + pos, buf_size - pos, "\\\\"); }
      else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F))  { pos += snprintf(buf + pos, buf_size - pos, "\\u%04x", c); }
      else                { buf[pos++] = (char)c; }
    }
    if (pos < buf_size - 2) buf[pos++] = '"';
  }
  if (!this->serial_number_.empty() && pos < buf_size - 128) {
    pos += snprintf(buf + pos, buf_size - pos, ",\"serial_number\":\"");
    for (unsigned char c : this->serial_number_) {
      if (pos >= buf_size - 8) break;
      if      (c == '"')  { pos += snprintf(buf + pos, buf_size - pos, "\\\""); }
      else if (c == '\\') { pos += snprintf(buf + pos, buf_size - pos, "\\\\"); }
      else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F))  { pos += snprintf(buf + pos, buf_size - pos, "\\u%04x", c); }
      else                { buf[pos++] = (char)c; }
    }
    if (pos < buf_size - 2) buf[pos++] = '"';
  }
  if (pos < buf_size - 2) buf[pos++] = '}';
  buf[pos] = '\0';
  return buf;
}

// Helper: strncpy with guaranteed null termination.
static inline void safe_strncpy(char* dst, const char* src, size_t dst_size)
{
  if (dst_size == 0) return;
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

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

void HaDiscoveryManager::discover_stale_topics_()
{
  // Stub: skip stale discovery, go straight to complete (tests expect COMPLETE).
  this->state_ = HA_DISCOVERY_COMPLETE;
}

void HaDiscoveryManager::stale_topic_callback_(const char* topic, const char* payload, size_t payload_len, void* user_data)
{
  (void)topic; (void)payload; (void)payload_len; (void)user_data;
}

void HaDiscoveryManager::publish_stale_cleanup_()
{
  this->state_ = HA_DISCOVERY_IDLE;
}
#else  /* !USE_ESP_IDF_STUBS — real ESP-IDF implementation */

// Minimal zero-allocation JSON string extractor for flat JSON objects.
// Finds "key":"value" in JSON and returns a pointer to the first character
// of the value (after the opening quote), or "" if the key is not found or
// the value is not a string.
//
// Key matching is exact: the key must be a JSON string (quoted), and the
// match must be at a structural boundary (after { or ,) to avoid matching
// the key name inside a value string.
static const char* json_get_str(const char* json, const char* key)
{
  // Finds "key":"value" in flat JSON objects. Returns a pointer to the first
  // character of the value (after the opening quote), or "" if the key is not
  // found or the value is not a string.
  //
  // Structural validation: a JSON key must be preceded by { or , (with optional
  // whitespace), ensuring we don't match a key name that appears inside a value.
  int key_len = (int)strlen(key);
  if (key_len == 0) return "";

  const char* p = json;
  while ((p = strstr(p, "\"")) != nullptr) {
    // Verify this quoted string is at a structural key position:
    // preceded by { or , (with optional whitespace/newlines).
    {
      const char* prev = p - 1;
      while (prev >= json && (*prev == ' ' || *prev == '\t' || *prev == '\n' || *prev == '\r'))
        prev--;
      if (prev < json || (*prev != '{' && *prev != ',')) {
        // Not a key position — advance past this quoted string.
        p++;
        while (*p && *p != '"') {
          if (*p == '\\') p++;
          p++;
        }
        if (*p == '"') p++;
        continue;
      }
    }

    // Check if this quoted string is our key.
    const char* key_start = p + 1;
    const char* q = key_start;
    int i = 0;
    bool match = true;
    while (i < key_len) {
      if (*q == '\\') {
        // Escaped character in key - not a simple match
        match = false;
        break;
      }
      if (*q == '"' || *q == '\0') {
        match = false;
        break;
      }
      if (*q != key[i]) {
        match = false;
        break;
      }
      q++;
      i++;
    }
    if (match && *q == '"') {
      // Found the key. Now find the colon and value.
      q++; // skip closing quote
      while (*q == ' ' || *q == '\t') q++;
      if (*q == ':') {
        q++; // skip colon
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '"') {
          return q + 1; // point to first char of value
        }
        // Value is not a string (number, bool, null)
        return "";
      }
    }
    // Not our key - advance past this quoted string
    p = q;
    while (*p && *p != '"') {
      if (*p == '\\') p++; // skip escaped char
      p++;
    }
    if (*p == '"') p++; // skip closing quote
  }
  return "";
}

/*static*/ void HaDiscoveryManager::ha_fetch_task_fn_(void* param)
{
  auto* self = static_cast<HaDiscoveryManager*>(param);
  self->fetch_ha_definitions_();
  // Send sentinel to signal completion to the main loop.
  HaDiscoveryItem* sentinel = nullptr;
  xQueueSend(self->queue_, &sentinel, pdMS_TO_TICKS(5000));
  vTaskDelete(nullptr);
}

void HaDiscoveryManager::fetch_ha_definitions_()
{
  struct CategoryRange { const char* name; uint16_t lo; uint16_t hi; };
  static const CategoryRange CATS[] = {
    {"common",0x0000,0x0FFF},{"refrigeration",0x1000,0x1FFF},{"laundry",0x2000,0x2FFF},
    {"dishwasher",0x3000,0x3FFF},{"waterheater",0x4000,0x4FFF},{"range",0x5000,0x5FFF},
    {"airconditioning",0x7000,0x7FFF},{"waterfilter",0x8000,0x8FFF},
    {"smallappliance",0x9000,0x9FFF},{"energy",0xD000,0xDFFF},
  };
  static constexpr int NUM_CATS = 10;
  bool need[NUM_CATS] = {};
  need[0] = true;  // Always include common

  // Build a sorted array of ERDs from the cache for O(log n) lookup.
  this->sorted_erds_count_ = 0;
  uint16_t iterator = 0;
  while (true) {
    erd_cache_entry_t* entry = erd_cache_get_next_entry(this->erd_cache_, &iterator);
    if (!entry) break;
    if (this->sorted_erds_count_ < HA_DISCOVERY_MAX_ERDS) {
      this->sorted_erds_[this->sorted_erds_count_++] = entry->erd;
    }
    for (int j = 1; j < NUM_CATS; ++j)
      if (entry->erd >= CATS[j].lo && entry->erd <= CATS[j].hi) { need[j] = true; break; }
  }
  // Sort the ERD array for binary search (simple insertion sort, small N).
  for (uint16_t i = 1; i < this->sorted_erds_count_; i++) {
    tiny_erd_t key = this->sorted_erds_[i];
    uint16_t j = i;
    while (j > 0 && this->sorted_erds_[j - 1] > key) {
      this->sorted_erds_[j] = this->sorted_erds_[j - 1];
      j--;
    }
    this->sorted_erds_[j] = key;
  }

  const char* device_json = this->build_device_json_();

  // For each needed category, find the matching embedded data and process it.
  for (int i = 0; i < NUM_CATS; ++i) {
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
                                           const char* device_json)
{
#ifdef USE_ESP_IDF_STUBS
  ESP_LOGE(TAG, "HA fetch: decompression not available in stub build");
  return false;
#else
  // Each category is split into independently-compressible chunks.
  // Decompress each chunk into the pre-allocated buffer, process lines,
  // then move to the next chunk — buffer reused throughout.
  if (cat->max_decompressed_chunk > HA_DECOMP_BUF_SIZE) {
    ESP_LOGE(TAG, "HA fetch: chunk %u bytes exceeds buffer %u",
             static_cast<unsigned>(cat->max_decompressed_chunk),
             static_cast<unsigned>(HA_DECOMP_BUF_SIZE));
    return false;
  }

  int line_pos = 0;
  int entities = 0;

  for (uint16_t ci = 0; ci < cat->num_chunks; ci++) {
    const HaDiscoveryChunk* chunk = &cat->chunks[ci];
    const uint8_t* src = cat->data + chunk->offset;
    size_t src_size = chunk->size;
    size_t dst_size = cat->max_decompressed_chunk;

    tinfl_init(&this->decomp_state_);

    tinfl_status status = tinfl_decompress(
        &this->decomp_state_, src, &src_size,
        this->decomp_buf_, this->decomp_buf_, &dst_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if (status != TINFL_STATUS_DONE) {
      ESP_LOGE(TAG, "HA fetch: chunk %u of %s failed (status %d)", ci, cat->name, status);
      return false;
    }

    // Process decompressed bytes as lines.
    for (size_t i = 0; i < dst_size; i++) {
      char ch = static_cast<char>(this->decomp_buf_[i]);
      if (ch == '\n' || ch == '\r') {
        if (line_pos > 2) {
          this->line_buf_[line_pos] = '\0';
          if (process_jsonl_line_(this->line_buf_, device_id, device_json)) {
            entities++;
          }
        }
        line_pos = 0;
      } else if (line_pos < 4095) {
        this->line_buf_[line_pos++] = ch;
      }
    }
  }

  // Process any remaining data in line buffer.
  if (line_pos > 2) {
    this->line_buf_[line_pos] = '\0';
    if (process_jsonl_line_(this->line_buf_, device_id, device_json)) {
      entities++;
    }
  }

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
  if (xQueueReceive(this->queue_, &item, 0) != pdTRUE) {
    // Queue is empty. If the task has terminated (sentinel send timed out),
    // clean up and transition to stale discovery.
    if (this->task_handle_ == nullptr) {
      vQueueDelete(this->queue_); this->queue_ = nullptr;
      free(this->task_stack_); free(this->task_tcb_);
      this->task_stack_ = nullptr; this->task_tcb_ = nullptr;
      ESP_LOGI(TAG, "HA discovery complete — %u entities published",
               static_cast<unsigned>(this->published_topics_count_));
      this->discover_stale_topics_();
      return;
    }
    return;
  }

  // Sentinel — fetch task is done.
  if (item == nullptr) {
    vQueueDelete(this->queue_); this->queue_ = nullptr;
    free(this->task_stack_); free(this->task_tcb_);
    this->task_stack_ = nullptr; this->task_tcb_ = nullptr;
    this->task_handle_ = nullptr;
    ESP_LOGI(TAG, "HA discovery complete — %u entities published",
             static_cast<unsigned>(this->published_topics_count_));
    this->discover_stale_topics_();
    return;
  }

  // Publish the entity.
  if (this->mqtt_adapter_) {
    esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
  }
  delete item;
}
bool HaDiscoveryManager::process_jsonl_line_(const char* line,
                                              const std::string& device_id,
                                              const char* device_json)
{
  // Extract a JSON string value, unescaping \\ and \\" as it copies.
  // Result is written into 'out' (caller must provide buffer).
  auto get_str = [&](const char* key, char* out, int out_size) {
    const char* val = json_get_str(line, key);
    if (val[0] == '\0') { out[0] = '\0'; return; }
    int i = 0;
    const char* p = val;
    while (*p != '\0' && *p != '"') {
      if (*p == '\\') {
        p++;
        switch (*p) {
          case '"':  if (i < out_size - 1) out[i++] = '"'; break;
          case '\\': if (i < out_size - 1) out[i++] = '\\'; break;
          case '/':  if (i < out_size - 1) out[i++] = '/'; break;
          case 'n':  if (i < out_size - 1) out[i++] = '\n'; break;
          case 'r':  if (i < out_size - 1) out[i++] = '\r'; break;
          case 't':  if (i < out_size - 1) out[i++] = '\t'; break;
          default:   if (i < out_size - 1) out[i++] = *p; break;
        }
        p++;
      } else {
        if (i < out_size - 1) out[i++] = *p;
        p++;
      }
    }
    out[i] = '\0';
  };

  char val_buf[128];
  get_str("i", val_buf, sizeof(val_buf));
  const char* erd_hex = val_buf;
  const char* unique_suffix = erd_hex;
  if (erd_hex[0] == '\0') return false;
  uint16_t erd_id = static_cast<uint16_t>(strtol(erd_hex, nullptr, 16));

  get_str("r", val_buf, sizeof(val_buf));
  const char* role = val_buf;
  get_str("p", val_buf, sizeof(val_buf));
  const char* paired = val_buf;

  // Check if this ERD exists in the sorted cache array (binary search).
  if (this->erd_cache_ != nullptr) {
    bool found = false;
    {
      uint16_t lo = 0, hi = this->sorted_erds_count_;
      while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        if (this->sorted_erds_[mid] < erd_id) lo = mid + 1;
        else if (this->sorted_erds_[mid] > erd_id) hi = mid;
        else { found = true; break; }
      }
    }
    // Check paired ERD if not found and this is a request role with a paired ERD.
    if (!found && role[0] == 'r' && paired[0] != '\0') {
      uint16_t paired_id = static_cast<uint16_t>(strtol(paired, nullptr, 16));
      if (paired_id) {
        uint16_t lo = 0, hi = this->sorted_erds_count_;
        while (lo < hi) {
          uint16_t mid = lo + (hi - lo) / 2;
          if (this->sorted_erds_[mid] < paired_id) lo = mid + 1;
          else if (this->sorted_erds_[mid] > paired_id) hi = mid;
          else { found = true; break; }
        }
      }
    }
    if (!found) return false;
  }

  char comp_buf[128];
  get_str("d", comp_buf, sizeof(comp_buf));
  const char* comp = comp_buf;
  if (comp[0] == '\0') return false;

  // Build payload on a stack buffer — no heap allocation.
  char payload_buf[1024];
  char esc_buf[128];
  int pos = 0;
  auto esc = [&](const char* s) -> const char* {
    escape_json_str_(s, esc_buf, sizeof(esc_buf));
    return esc_buf;
  };
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      "{\"device\":%s", device_json);

  get_str("n", val_buf, sizeof(val_buf));
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      ",\"name\":\"%s\"", esc(val_buf));
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      ",\"unique_id\":\"%s_%s\"", device_id.c_str(), unique_suffix);

  get_str("o", val_buf, sizeof(val_buf));
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
      ",\"object_id\":\"%s\"", esc(val_buf));

  get_str("u", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"unit_of_measurement\":\"%s\"", esc(val_buf));
  }
  get_str("ic", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"icon\":\"%s\"", esc(val_buf));
  }
  get_str("dc", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"device_class\":\"%s\"", esc(val_buf));
  }
  get_str("e", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"entity_category\":\"%s\"", esc(val_buf));
  }
  get_str("s", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"state_topic\":\"%s\"", esc(val_buf));
  }
  get_str("c", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"command_topic\":\"%s\"", esc(val_buf));
  }
  get_str("on", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"payload_on\":\"%s\"", esc(val_buf));
  }
  get_str("of", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"payload_off\":\"%s\"", esc(val_buf));
  }
  get_str("a", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"availability_topic\":\"%s\"", esc(val_buf));
  }
  get_str("j", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"json_attributes_topic\":\"%s\"", esc(val_buf));
  }
  get_str("v", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"value_template\":\"%s\"", esc(val_buf));
  }
  get_str("cm", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos,
        ",\"command_template\":\"%s\"", esc(val_buf));
  }
  get_str("opt", val_buf, sizeof(val_buf));
  if (val_buf[0] != '\0') {
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, ",\"options\":[");
    bool first = true;
    for (const char* p = val_buf; *p; ) {
      const char* comma = strchr(p, ',');
      char opt_val[64];
      int vlen;
      if (comma) {
        vlen = (int)(comma - p);
        if (vlen >= (int)sizeof(opt_val)) vlen = (int)sizeof(opt_val) - 1;
      } else {
        vlen = (int)strlen(p);
        if (vlen >= (int)sizeof(opt_val)) vlen = (int)sizeof(opt_val) - 1;
        p += vlen;
      }
      memcpy(opt_val, p, vlen);
      opt_val[vlen] = '\0';
      p = comma ? comma + 1 : p + vlen;
      if (!first) pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, ",");
      pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, "\"%s\"", esc(opt_val));
      first = false;
    }
    pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, "]");
  }
  pos += snprintf(payload_buf + pos, sizeof(payload_buf) - pos, "}");

  if (pos >= (int)sizeof(payload_buf)) {
    ESP_LOGW(TAG, "HA fetch: payload too large for ERD %s", erd_hex);
    return false;
  }

  // Build topic on stack.
  char topic_buf[128];
  snprintf(topic_buf, sizeof(topic_buf),
      "homeassistant/%s/%s/%s/config", comp, device_id.c_str(), unique_suffix);

  // Insert in sorted order (by component, then erd_hex) for binary search.
  if (this->published_topics_count_ < HA_DISCOVERY_MAX_PUBLISHED_TOPICS) {
    auto& t = this->published_topics_[this->published_topics_count_];
    safe_strncpy(t.component, comp, sizeof(t.component));
    safe_strncpy(t.erd_hex, unique_suffix, sizeof(t.erd_hex));
    // Shift elements to maintain sorted order (insertion sort on append).
    int insert_idx = this->published_topics_count_;
    while (insert_idx > 0) {
      int cmp = strcmp(this->published_topics_[insert_idx - 1].component, t.component);
      if (cmp < 0 || (cmp == 0 && strcmp(this->published_topics_[insert_idx - 1].erd_hex, t.erd_hex) < 0))
        break;
      this->published_topics_[insert_idx] = this->published_topics_[insert_idx - 1];
      insert_idx--;
    }
    this->published_topics_[insert_idx] = t;
    this->published_topics_count_++;
  }

  // Queue the item for the main loop to publish.
  auto* item = new HaDiscoveryItem();
  safe_strncpy(item->topic, topic_buf, sizeof(item->topic));
  safe_strncpy(item->payload, payload_buf, sizeof(item->payload));

  if (this->queue_) {
    if (xQueueSend(this->queue_, &item, pdMS_TO_TICKS(500)) != pdTRUE) {
      ESP_LOGW(TAG, "HA fetch: queue full after 500ms, dropping entity for ERD %s", erd_hex);
      delete item;
    }
  } else {
    delete item;
  }

  return true;
}

void HaDiscoveryManager::discover_stale_topics_()
{
  // Reset stale topic collection.
  this->stale_topics_count_ = 0;
  this->stale_cleanup_index_ = 0;

  if (!this->mqtt_adapter_) {
    ESP_LOGW(TAG, "No MQTT adapter, skipping stale topic discovery");
    this->state_ = HA_DISCOVERY_IDLE;
    return;
  }

  // Subscribe to homeassistant/*/<device_id>/*/config to discover all retained topics.
  char topic_pattern[128];
  snprintf(topic_pattern, sizeof(topic_pattern),
           "homeassistant/*/%s/*/config", this->device_id_.c_str());

  this->stale_subscription_handle_ = esphome_mqtt_client_adapter_subscribe(
      this->mqtt_adapter_,
      topic_pattern,
      &HaDiscoveryManager::stale_topic_callback_,
      this);

  if (!this->stale_subscription_handle_) {
    ESP_LOGW(TAG, "Failed to subscribe for stale topic discovery");
    this->state_ = HA_DISCOVERY_IDLE;
    return;
  }

  this->stale_discovery_start_ms_ = millis();
  this->state_ = HA_DISCOVERY_CLEANING_STALE;
  ESP_LOGI(TAG, "Discovering stale HA topics from broker: %s", topic_pattern);
}

void HaDiscoveryManager::stale_topic_callback_(const char* topic, const char* payload, size_t payload_len, void* user_data)
{
  if (user_data == nullptr) return;
  auto* self = static_cast<HaDiscoveryManager*>(user_data);

  // Parse topic: homeassistant/<component>/<device_id>/<erd_hex>/config
  // Extract component and erd_hex to check against published_topics_.
  // Format: homeassistant/sensor/mydevice/0002/config
  const char* p = topic;

  // Skip "homeassistant/"
  p = strstr(p, "homeassistant/");
  if (!p) return;
  p += 12; // len("homeassistant/")

  // Read component (up to next /)
  char component[32] = {0};
  int i = 0;
  while (*p && *p != '/' && i < 31) { component[i++] = *p++; }
  component[i] = '\0';

  // Skip device_id (next segment)
  if (*p == '/') p++;
  while (*p && *p != '/') p++;

  // Read erd_hex (next segment, before /config)
  if (*p == '/') p++;
  char erd_hex[32] = {0};
  i = 0;
  while (*p && *p != '/' && i < 31) { erd_hex[i++] = *p++; }
  erd_hex[i] = '\0';

  // Binary search in sorted published_topics_ (sorted by component, then erd_hex).
  bool found = false;
  {
    uint16_t lo = 0, hi = self->published_topics_count_;
    while (lo < hi) {
      uint16_t mid = lo + (hi - lo) / 2;
      int cmp = strcmp(self->published_topics_[mid].component, component);
      if (cmp < 0) { lo = mid + 1; continue; }
      if (cmp > 0) { hi = mid; continue; }
      // Same component — compare erd_hex.
      cmp = strcmp(self->published_topics_[mid].erd_hex, erd_hex);
      if (cmp < 0) lo = mid + 1;
      else if (cmp > 0) hi = mid;
      else { found = true; break; }
    }
  }

  if (!found) {
    // This is a stale topic — add to cleanup list if not already present.
    // Use binary search since stale_topics_ is maintained in sorted order.
    bool already_in_list = false;
    {
      uint16_t lo = 0, hi = self->stale_topics_count_;
      while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(self->stale_topics_[mid].topic, topic);
        if (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid;
        else { already_in_list = true; break; }
      }
    }
    if (!already_in_list && self->stale_topics_count_ < HA_DISCOVERY_MAX_PUBLISHED_TOPICS) {
      // Insert in sorted order.
      uint16_t insert_idx = self->stale_topics_count_;
      while (insert_idx > 0 && strcmp(self->stale_topics_[insert_idx - 1].topic, topic) > 0) {
        self->stale_topics_[insert_idx] = self->stale_topics_[insert_idx - 1];
        insert_idx--;
      }
      snprintf(self->stale_topics_[insert_idx].topic,
               sizeof(self->stale_topics_[0].topic), "%s", topic);
      self->stale_topics_count_++;
      ESP_LOGD(TAG, "Found stale HA topic: %s", topic);
    }
  }
}

void HaDiscoveryManager::publish_stale_cleanup_()
{
  if (this->stale_cleanup_index_ >= this->stale_topics_count_) {
    // All stale topics cleaned up.
    ESP_LOGI(TAG, "Stale topic cleanup complete — removed %u topics",
             static_cast<unsigned>(this->stale_topics_count_));
    this->stale_topics_count_ = 0;
    this->stale_cleanup_index_ = 0;
    this->state_ = HA_DISCOVERY_IDLE;
    return;
  }

  // Publish empty retained message to delete the stale topic.
  const char* stale_topic = this->stale_topics_[this->stale_cleanup_index_].topic;
  ESP_LOGD(TAG, "Cleaning stale topic %u/%u: %s",
           static_cast<unsigned>(this->stale_cleanup_index_ + 1),
           static_cast<unsigned>(this->stale_topics_count_),
           stale_topic);

  if (this->mqtt_adapter_) {
    esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, stale_topic, "", true);
  }
  this->stale_cleanup_index_++;
}

#endif  /* USE_ESP_IDF_STUBS */

}  // namespace geappliances_bridge
}  // namespace esphome
