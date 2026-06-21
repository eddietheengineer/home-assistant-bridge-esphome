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

    // Wait for the task to signal it has entered the termination path
    // (gives the semaphore before calling vTaskDelete).  Use non-blocking
    // xSemaphoreTake in a yield loop — blocking xSemaphoreTake uses
    // vTaskDelay internally which crashes on ESP32-C6 via vSystimerSetup.
    bool signaled = false;
    if (this->done_semaphore_ != nullptr) {
      uint32_t start = millis();
      while (!signaled && millis() - start < 5000) {
        if (xSemaphoreTake(this->done_semaphore_, 0) == pdTRUE) {
          signaled = true;
        } else {
          esp_task_wdt_reset();
          taskYIELD();
        }
      }
      if (!signaled) {
        ESP_LOGW(TAG, "HA discovery task did not signal done within 5 s");
      }
    } else if (this->task_handle_ != nullptr) {
      // Fallback: poll with yield when semaphore was never created.
      uint32_t start = millis();
      while (this->task_handle_ != nullptr && millis() - start < 5000) {
        esp_task_wdt_reset();
        taskYIELD();
      }
      if (this->task_handle_ != nullptr) {
        ESP_LOGW(TAG, "HA discovery task did not terminate within 5 s");
      }
    }

    // Wait for the idle task to unlink the TCB from
    // xTasksWaitingTermination before calling vQueueDelete, which
    // triggers prvCheckTasksWaitingTermination and crashes on ESP32-C6.
    if (this->task_handle_ != nullptr) {
      uint32_t start = millis();
      while (eTaskGetState(this->task_handle_) != eInvalid &&
             millis() - start < 5000) {
        esp_task_wdt_reset();
        taskYIELD();
      }
      if (eTaskGetState(this->task_handle_) != eInvalid) {
        ESP_LOGW(TAG, "HA discovery task TCB not cleaned within 5 s");
      }
    }
  }
  // Delete the queue.
  if (this->queue_ != nullptr) {
    vQueueDelete(this->queue_);
    this->queue_ = nullptr;
  }
  // Clean up the semaphore.
  if (this->done_semaphore_ != nullptr) {
    vSemaphoreDelete(this->done_semaphore_);
    this->done_semaphore_ = nullptr;
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

// Zero-allocation JSON string extractor for flat JSON objects.
// Finds "key":"value" in JSON and returns a pointer into the source string.
// Returns "" if key not found or value is not a string.
static const char* json_get_str(const char* json, const char* key)
{
  char pattern[32];
  int plen = (int)snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  if (plen <= 0 || plen >= (int)sizeof(pattern) - 1) return "";

  const char* p = json;
  while ((p = strstr(p, pattern)) != nullptr) {
    p += plen;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
      return p + 1;
    }
    p++;
  }
  return "";
}

/*static*/ void HaDiscoveryManager::ha_fetch_task_fn_(void* param)
{
  auto* self = static_cast<HaDiscoveryManager*>(param);
  self->fetch_ha_definitions_();
  // Send sentinel to signal completion to the main loop.
  HaDiscoveryItem* sentinel = nullptr;
  xQueueSend(self->queue_, &sentinel, portMAX_DELAY);
  // Signal the cleanup path that we're about to exit, before calling
  // vTaskDelete so the caller can wait for termination + idle-task TCB
  // cleanup without polling a handle the task never clears.
  if (self->done_semaphore_ != nullptr) {
    xSemaphoreGive(self->done_semaphore_);
  }
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

  // tinfl_decompressor is ~10KB on ESP32-C3 (includes 32KB LZ dictionary).
  // Allocate from heap to avoid blowing the task stack.
  tinfl_decompressor* decomp = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
  if (!decomp) {
    ESP_LOGE(TAG, "HA fetch: failed to allocate decompressor");
    free(out_buf);
    return false;
  }

  char* line_buf = static_cast<char*>(malloc(4096));
  if (!line_buf) {
    ESP_LOGE(TAG, "HA fetch: failed to allocate line buffer");
    free(decomp);
    free(out_buf);
    return false;
  }
  int line_pos = 0;
  int entities = 0;

  for (uint16_t ci = 0; ci < cat->num_chunks; ci++) {
    const HaDiscoveryChunk* chunk = &cat->chunks[ci];
    const uint8_t* src = cat->data + chunk->offset;
    size_t src_size = chunk->size;
    size_t dst_size = cat->max_decompressed_chunk;

    tinfl_init(decomp);

    tinfl_status status = tinfl_decompress(
        decomp, src, &src_size,
        out_buf, out_buf, &dst_size,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    if (status != TINFL_STATUS_DONE) {
      ESP_LOGE(TAG, "HA fetch: chunk %u of %s failed (status %d)", ci, cat->name, status);
      free(line_buf);
      free(decomp);
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
      } else if (line_pos < 4095) {
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

  free(line_buf);
  free(decomp);
  free(out_buf);
  ESP_LOGI(TAG, "HA fetch: %s -> %d entities", cat->name, entities);
  return true;
#endif
}

void HaDiscoveryManager::publish_ha_discovery_()
{
  // Use xTaskCreate so FreeRTOS owns the TCB/stack lifecycle.
  // After vTaskDelete() the idle task frees both automatically,
  // eliminating the TCB use-after-free that crashed on ESP32-C6.
  static constexpr int STACK_SIZE = 48 * 1024;  // 48 KB
  this->queue_ = xQueueCreateStatic(64, sizeof(HaDiscoveryItem*),
      static_cast<uint8_t*>(heap_caps_malloc(64 * sizeof(HaDiscoveryItem*), MALLOC_CAP_8BIT)),
      static_cast<StaticQueue_t*>(heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_8BIT)));
  if (!this->queue_) {
    ESP_LOGE(TAG, "Failed to create queue for HA discovery task");
    this->state_ = HA_DISCOVERY_FAILED;
    return;
  }
  this->done_semaphore_ = xSemaphoreCreateBinary();
  if (xTaskCreate(ha_fetch_task_fn_, "ha_fetch", STACK_SIZE, this, 1,
      &this->task_handle_) != pdTRUE) {
    vSemaphoreDelete(this->done_semaphore_); this->done_semaphore_ = nullptr;
    vQueueDelete(this->queue_); this->queue_ = nullptr;
    this->task_handle_ = nullptr;
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
      // Sentinel — fetch task has called vTaskDelete().  On ESP32-C6 the
      // idle task must unlink the TCB from xTasksWaitingTermination BEFORE
      // we call vQueueDelete(), because prvUnlockQueue triggers
      // prvCheckTasksWaitingTermination which crashes when it tries to
      // uxListRemove a TCB whose list items are in an inconsistent state.
      // Yield in a tight loop until eTaskGetState reports eInvalid (TCB
      // cleaned up), then it's safe to delete the queue.
      if (this->task_handle_ != nullptr) {
        uint32_t start = millis();
        while (eTaskGetState(this->task_handle_) != eInvalid &&
               millis() - start < 5000) {
          taskYIELD();
        }
        if (eTaskGetState(this->task_handle_) != eInvalid) {
          ESP_LOGW(TAG, "HA discovery task TCB not cleaned within 5 s");
        }
      }
      this->task_handle_ = nullptr;
      vQueueDelete(this->queue_); this->queue_ = nullptr;
      if (this->done_semaphore_ != nullptr) {
        vSemaphoreDelete(this->done_semaphore_);
        this->done_semaphore_ = nullptr;
      }
      ESP_LOGI(TAG, "HA discovery complete — %u entities published",
               static_cast<unsigned>(this->published_topics_count_));
      this->discover_stale_topics_();
    }
    if (this->mqtt_adapter_) {
      esphome_mqtt_client_adapter_publish(this->mqtt_adapter_, item->topic, item->payload, true);
    }
    delete item;
  }
}
bool HaDiscoveryManager::process_jsonl_line_(const char* line,
                                              const std::string& device_id,
                                              const std::string& device_json)
{
  // Extract a JSON string value, terminating at the closing quote.
  // Result is written into 'out' (caller must provide buffer).
  auto get_str = [&](const char* key, char* out, int out_size) {
    const char* val = json_get_str(line, key);
    if (val[0] == '\0') { out[0] = '\0'; return; }
    int i = 0;
    while (val[i] != '\0' && val[i] != '"') {
      if (i < out_size - 1) out[i] = val[i];
      i++;
    }
    out[i] = '\0';
  };

  char val_buf[128];
  get_str("i", val_buf, sizeof(val_buf));
  const char* erd_hex = val_buf;
  if (erd_hex[0] == '\0') return false;
  uint16_t erd_id = static_cast<uint16_t>(strtol(erd_hex, nullptr, 16));

  get_str("r", val_buf, sizeof(val_buf));
  const char* role = val_buf;
  get_str("p", val_buf, sizeof(val_buf));
  const char* paired = val_buf;

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
    if (!found) return false;
  }

  char fi_buf[128];
  get_str("fi", fi_buf, sizeof(fi_buf));
  char unique_suffix[64];
  if (fi_buf[0] != '\0') {
    snprintf(unique_suffix, sizeof(unique_suffix), "%s_%s", erd_hex, fi_buf);
  } else {
    snprintf(unique_suffix, sizeof(unique_suffix), "%s", erd_hex);
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
      "{\"device\":%s", device_json.c_str());

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

  // Track this topic for later clearing
  if (this->published_topics_count_ < HA_DISCOVERY_MAX_PUBLISHED_TOPICS) {
    auto& t = this->published_topics_[this->published_topics_count_];
    strncpy(t.component, comp, sizeof(t.component) - 1);
    t.component[sizeof(t.component) - 1] = '\0';
    strncpy(t.erd_hex, unique_suffix, sizeof(t.erd_hex) - 1);
    t.erd_hex[sizeof(t.erd_hex) - 1] = '\0';
    this->published_topics_count_++;
  }

  // Queue the item for the main loop to publish.
  // Blocks if queue is full — backpressures the fetch task.
  auto* item = new HaDiscoveryItem();
  strncpy(item->topic, topic_buf, sizeof(item->topic) - 1);
  item->topic[sizeof(item->topic) - 1] = '\0';
  strncpy(item->payload, payload_buf, sizeof(item->payload) - 1);
  item->payload[sizeof(item->payload) - 1] = '\0';

  if (this->queue_) {
    if (xQueueSend(this->queue_, &item, pdMS_TO_TICKS(2000)) != pdTRUE) {
      ESP_LOGW(TAG, "HA fetch: queue full after 2s, dropping entity for ERD %s", erd_hex);
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

  // Check if this topic is in our published set.
  bool found = false;
  for (uint16_t j = 0; j < self->published_topics_count_; j++) {
    if (strcmp(self->published_topics_[j].component, component) == 0 &&
        strcmp(self->published_topics_[j].erd_hex, erd_hex) == 0) {
      found = true;
      break;
    }
  }

  if (!found) {
    // This is a stale topic — add to cleanup list if not already present.
    bool already_in_list = false;
    for (uint16_t j = 0; j < self->stale_topics_count_; j++) {
      if (strcmp(self->stale_topics_[j].topic, topic) == 0) {
        already_in_list = true;
        break;
      }
    }
    if (!already_in_list && self->stale_topics_count_ < HA_DISCOVERY_MAX_PUBLISHED_TOPICS) {
      snprintf(self->stale_topics_[self->stale_topics_count_].topic,
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
