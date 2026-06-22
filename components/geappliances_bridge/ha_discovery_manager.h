/*!
 * @file
 * @brief HaDiscoveryManager – Home Assistant MQTT autodiscovery manager.
 *
 * Modular, fire-and-forget component: the bridge calls configure() to set
 * device identity, then start() once at steady state to begin discovery.
 * The manager spawns a FreeRTOS background task to decompress embedded
 * JSONL definitions, match them against the ERD cache, and publish
 * discovery payloads at a controlled rate.
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
//   - Decompress embedded JSONL entity definitions from flash
//   - Match definitions against the ERD cache via binary search
//   - Build and rate-limit publish discovery payloads to Home Assistant
//   - Discover and clean up stale discovery topics from previous configurations
//
// NOT responsible for:
//   - Determining when to start (the bridge gates on steady state)
//   - Determining which ERDs are valid (reads the ERD cache directly)
//   - Managing bridge lifecycle or MQTT connection state
//   - Any post-discovery entity updates
//
// Dependencies:
//   - EsphomeMqttClientAdapter (publish/subscribe)
//   - FreeRTOS task + queue on ESP-IDF builds (for fetching JSONL)
//   - erd_cache_t (opaque pointer, forward declared)
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

static constexpr uint32_t HA_ENTITY_PUBLISH_INTERVAL_MS = 50;

enum HaDiscoveryState {
  HA_DISCOVERY_IDLE,
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
#define HA_DISCOVERY_ITEM_POOL_SIZE 32  // items in flight (queue depth)
#define HA_DISCOVERY_ITEM_POOL_SENTINEL 0xFFFF  // sentinel for queue: fetch done
struct HaDiscoveryCategory;


class HaDiscoveryManager {
 public:
  /// Configure device identity and ERD cache pointer.
  /// Call once before start(). Does not change state.
  void configure(const std::string& device_id,
                 const std::string& model_number,
                 const std::string& serial_number,
                 erd_cache_t* erd_cache,
                 bool generate_device_config);

  /// Start the discovery process.
  /// The bridge must ensure the device is in steady state before calling.
  /// Transitions to PUBLISHING (or COMPLETE/FAILED on error).
  /// Idempotent: calling multiple times is safe (no-ops after first start).
  void start();

  /// Drive the state machine for publishing/cleanup phases.
  /// Called every loop iteration while in PUBLISHING, CLEARING, or CLEANING_STALE.
  void run();

  /// Set the MQTT adapter for async publishing (typed pointer, nullptr = sync fallback)
  void set_mqtt_adapter(esphome_mqtt_client_adapter_t* mqtt_adapter);

  bool is_complete() const { return state_ == HA_DISCOVERY_COMPLETE; }
  bool is_failed()   const { return state_ == HA_DISCOVERY_FAILED; }
  bool is_publishing() const { return state_ == HA_DISCOVERY_PUBLISHING; }

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

  // Track published discovery topics for clearing later.
  // Fixed-size static array to avoid heap allocation spikes during discovery
  #ifdef USE_ESP_IDF_STUBS
  struct PublishedTopic {
    char component[32];  // e.g. "sensor", "switch", "binary_sensor"
    char erd_hex[32];    // e.g. "0002", "2001"
  };
  PublishedTopic published_topics_[HA_DISCOVERY_MAX_PUBLISHED_TOPICS];
  #else
  // On real ESP32, use a smaller fixed array to save BSS;
  // excess entities are published but not tracked for clearing.
  struct PublishedTopic {
    char component[32];
    char erd_hex[32];
  };
  static constexpr uint16_t HA_DISCOVERY_PUBLISHED_TOPICS_CAP = 256;
  PublishedTopic published_topics_[HA_DISCOVERY_PUBLISHED_TOPICS_CAP];
  #endif
  uint16_t published_topics_count_{0};
  uint16_t clear_index_{0};

  // Stale topic cleanup — fixed-size static array
  mqtt_subscription_handle_t stale_subscription_handle_{0};
  uint16_t stale_cleanup_index_{0};
  struct StaleTopic { char topic[128]; };
  #ifdef USE_ESP_IDF_STUBS
  StaleTopic stale_topics_[HA_DISCOVERY_MAX_PUBLISHED_TOPICS];
  #else
  static constexpr uint16_t HA_DISCOVERY_STALE_TOPICS_CAP = 64;
  StaleTopic stale_topics_[HA_DISCOVERY_STALE_TOPICS_CAP];
  #endif
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
  bool generate_device_config_{false};
  uint32_t last_publish_ms_{0};

  // Pointer to the MQTT adapter for async publishing (typed, set via set_mqtt_adapter)
  esphome_mqtt_client_adapter_t* mqtt_adapter_{nullptr};
  QueueHandle_t queue_{nullptr};  // queue of uint16_t indices into item_pool_
  TaskHandle_t  task_handle_{nullptr};
  bool fetch_done_{false};  // true once the fetch task has terminated (sentinel or timeout)
  StackType_t*  task_stack_{nullptr};
  StaticTask_t* task_tcb_{nullptr};
  // Pre-allocated decompression buffers (reused across categories).
  static constexpr uint16_t HA_DECOMP_BUF_SIZE = 4096;
  uint8_t decomp_buf_[HA_DECOMP_BUF_SIZE];
#ifndef USE_ESP_IDF_STUBS
  tinfl_decompressor decomp_state_;
#endif
  char line_buf_[4096];
  char device_json_buf_[512];
  // Pre-allocated pool of HaDiscoveryItem objects — avoids heap allocation
  // per entity during the fetch task, preventing OOM on constrained devices.
  HaDiscoveryItem item_pool_[HA_DISCOVERY_ITEM_POOL_SIZE];
  uint16_t item_pool_next_{0};  // round-robin index into item_pool_
};

}  // namespace geappliances_bridge
}  // namespace esphome
