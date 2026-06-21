/*!
 * @file
 * @brief HaDiscoveryManager – Home Assistant MQTT autodiscovery manager.
 *
 * Extracted from GeappliancesBridge as part of the god class refactoring.
 * Encapsulates the logic for:
 *   - Watching for "ready" signal (quiet window or polling list complete)
 *   - Spawning a FreeRTOS background task to fetch JSONL definitions via HTTPS
 *   - Parsing JSONL lines into MQTT discovery payloads
 *   - Rate-limited publishing of discovery messages to Home Assistant
 *
 * On non-ESP-IDF builds the fetch is a no-op and a warning is logged.
 */

// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Publish Home Assistant MQTT discovery payloads for the ERDs that the
//       bridge has registered at runtime.
//
// Responsibilities:
//   - Wait for a "ready" signal (quiet window or polling cycle complete)
//   - Spawn a FreeRTOS background task to fetch per-category JSONL definitions
//   - Parse JSONL lines, match against registered ERDs, build payloads
//   - Rate-limited publishing of discovery messages to Home Assistant
//
// NOT responsible for:
//   - Determining which ERDs are valid (receives registered ERD set externally)
//   - Managing bridge lifecycle or MQTT connection state
//   - Any post-discovery entity updates
//
// Dependencies:
//   - EsphomeMqttClientAdapter (debug log publish)
//   - FreeRTOS task + queue on ESP-IDF builds (for fetching JSONL)
// =============================================================================

#pragma once

#include <cstdint>
#include <string>

// Include the adapter header for the typed pointer (lightweight — no heavy deps)
#include "esphome_mqtt_client_adapter.h"

extern "C" {
#include "erd_cache.h"
#include "tiny_gea3_erd_client.h"
}

#ifndef USE_ESP_IDF
#  define USE_ESP_IDF_STUBS
#endif

#ifdef USE_ESP_IDF_STUBS
#  include "esp-idf/freertos_stub.h"
#else
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "freertos/queue.h"
#  include "miniz.h"
#endif

namespace esphome {
namespace geappliances_bridge {

static constexpr uint32_t HA_DISCOVERY_QUIET_MS = 10000;
static constexpr uint32_t HA_DISCOVERY_MAX_WAIT_MS = 30000;  // 30s safety cap
static constexpr uint32_t HA_ENTITY_PUBLISH_INTERVAL_MS = 50;

enum HaDiscoveryState {
  HA_DISCOVERY_IDLE,
  HA_DISCOVERY_WAITING_FOR_READY,
  HA_DISCOVERY_PUBLISHING,
  HA_DISCOVERY_COMPLETE,
  HA_DISCOVERY_CLEANING_STALE,  // discovering and cleaning stale topics
  HA_DISCOVERY_FAILED,
  HA_DISCOVERY_CLEARING  // clearing retained discovery topics
};

/*!
 * A (topic, payload) pair ready to be published via MQTT.
 */
struct HaDiscoveryItem {
  char topic[128];
  char payload[1024];
};

/* Maximum number of registered/seen ERDs for HA discovery. */
#define HA_DISCOVERY_MAX_ERDS 645
#define HA_DISCOVERY_MAX_PUBLISHED_TOPICS 645
struct HaDiscoveryCategory;


class HaDiscoveryManager {
 public:
  void init(const std::string& device_id,
            const std::string& model_number,
            const std::string& serial_number,
            erd_cache_t* erd_cache,
            bool generate_device_config);

  void set_registered_erds(const tiny_erd_t* erds, uint16_t count);

  void on_erd_seen(tiny_erd_t erd);

  /// Drive the state machine. Called every loop iteration.
  /// \param device_steady_state true when all active bridges are settled.
  void run(bool device_steady_state);

  /// Set the MQTT adapter for async publishing (typed pointer, nullptr = sync fallback)
  void set_mqtt_adapter(esphome_mqtt_client_adapter_t* mqtt_adapter);

  bool is_complete() const { return state_ == HA_DISCOVERY_COMPLETE; }
  bool is_failed()   const { return state_ == HA_DISCOVERY_FAILED; }
  bool is_publishing() const { return state_ == HA_DISCOVERY_PUBLISHING; }
  bool is_ready_to_start() const { return state_ == HA_DISCOVERY_WAITING_FOR_READY; }

  HaDiscoveryState get_state() const { return state_; }

  /// Synchronously clear all retained HA discovery topics (publish empty retained messages).
  /// Resets published_topics_count_ to 0 and sets state to IDLE.
  void clear_ha_discovery_sync();

  /// Clear all retained HA discovery topics for this device.
  /// Publishes empty retained payloads to each previously-published topic.
  /// Call this from the main loop context (ESPHome loop).
  void clear_ha_discovery();

  /// Clean up resources (FreeRTOS task, queue, stack). Call from teardown.
  void cleanup();

 private:
  void publish_ha_discovery_();
  void publish_next_entity_();
  void publish_next_clear_();
  void discover_stale_topics_();
  static void stale_topic_callback_(const char* topic, const char* payload, size_t payload_len, void* user_data);
  void publish_stale_cleanup_();


  static void ha_fetch_task_fn_(void* param);
  void fetch_ha_definitions_();
  bool process_category_(const HaDiscoveryCategory* cat,
                         const std::string& device_id,
                         const char* device_json);
  bool process_jsonl_line_(const char* line,
                           const std::string& device_id,
                           const char* device_json);

  int escape_json_str_(const char* s, char* buf, int buf_size);
  const char* build_device_json_();

  bool contains_erd_(const tiny_erd_t* erds, uint16_t count, tiny_erd_t target) const;

  // Track published discovery topics for clearing later.
  // Each entry stores the component type and ERD hex string.
  struct PublishedTopic {
    char component[32];  // e.g. "sensor", "switch", "binary_sensor"
    char erd_hex[32];    // e.g. "0002", "2001"
  };
  PublishedTopic published_topics_[HA_DISCOVERY_MAX_PUBLISHED_TOPICS];
  uint16_t published_topics_count_{0};
  uint16_t clear_index_{0};

  // Stale topic cleanup
  mqtt_subscription_handle_t stale_subscription_handle_{0};
  uint16_t stale_cleanup_index_{0};
  struct StaleTopic { char topic[128]; };
  StaleTopic stale_topics_[HA_DISCOVERY_MAX_PUBLISHED_TOPICS];
  uint16_t stale_topics_count_{0};
  uint32_t stale_discovery_start_ms_{0};

  HaDiscoveryState state_{HA_DISCOVERY_IDLE};
  std::string device_id_;
  std::string model_number_;
  std::string serial_number_;
  // Pointer to the ERD cache — read directly during fetch, no snapshot needed.
  erd_cache_t* erd_cache_{nullptr};
  // Sorted array of ERDs from the cache, built once at fetch start for binary search.
  tiny_erd_t sorted_erds_[HA_DISCOVERY_MAX_ERDS];
  uint16_t sorted_erds_count_{0};
  tiny_erd_t seen_erds_[HA_DISCOVERY_MAX_ERDS];
  uint16_t seen_erds_count_{0};
  bool generate_device_config_{false};
  uint32_t last_activity_{0};
  uint32_t last_publish_ms_{0};
  uint32_t start_time_{0};  // millis() when WAITING_FOR_READY state entered

  // Pointer to the MQTT adapter for async publishing (typed, set via set_mqtt_adapter)
  esphome_mqtt_client_adapter_t* mqtt_adapter_{nullptr};

  QueueHandle_t queue_{nullptr};
  TaskHandle_t  task_handle_{nullptr};
  bool fetch_done_{false};  // true once the fetch task has terminated (sentinel or timeout)
  StackType_t*  task_stack_{nullptr};
  StaticTask_t* task_tcb_{nullptr};
  // Pre-allocated decompression buffers (reused across categories).
  // Max decompressed chunk is typically ~2KB; line buffer 4KB.
  static constexpr uint16_t HA_DECOMP_BUF_SIZE = 4096;
  uint8_t decomp_buf_[HA_DECOMP_BUF_SIZE];
#ifndef USE_ESP_IDF_STUBS
  tinfl_decompressor decomp_state_;
#endif
  char line_buf_[4096];
  char device_json_buf_[512];
};

}  // namespace geappliances_bridge
}  // namespace esphome
