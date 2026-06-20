// Auto-generated — do not edit.
// Compressed JSONL data for HA discovery, embedded in flash.
// Each category is split into fixed-size zlib-compressed chunks for low-fragmentation
// streaming decompression on memory-constrained devices (ESP32-C3).
#pragma once

#include <stdint.h>

namespace esphome {
namespace geappliances_bridge {

// Single compressed chunk: offset+length into a shared flash byte array.
struct HaDiscoveryChunk {
  uint32_t offset;   // byte offset into the category's flash data
  uint16_t size;     // compressed size of this chunk
};

// Category: name, pointer to its flash data, chunk table, and chunk count.
struct HaDiscoveryCategory {
  const char* name;
  const uint8_t* data;              // flash data for all chunks concatenated
  const HaDiscoveryChunk* chunks;   // chunk table
  uint16_t num_chunks;
  uint16_t max_decompressed_chunk;  // max decompressed size of any chunk (for buffer sizing)
};

#ifdef USE_ESP_IDF_STUBS
// Test builds: no embedded data, empty arrays.
static const HaDiscoveryCategory ha_discovery_categories[] = { {nullptr, nullptr, nullptr, 0, 0} };
static const uint16_t ha_discovery_category_count = 0;
#else
// All categories - generated from ha_discovery/*.jsonl at build time.
extern const HaDiscoveryCategory ha_discovery_categories[];
extern const uint16_t ha_discovery_category_count;
#endif

}  // namespace geappliances_bridge
}  // namespace esphome
