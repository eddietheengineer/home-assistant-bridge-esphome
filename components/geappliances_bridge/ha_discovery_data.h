// Auto-generated — do not edit.
// Compressed JSONL data for HA discovery, embedded in flash.
// Each category is gzip-compressed; decompress at runtime into a static buffer.
#pragma once

#include <stdint.h>

namespace esphome {
namespace geappliances_bridge {

// Category structure: name, compressed data pointer, compressed size, decompressed size.
struct HaDiscoveryCategory {
  const char* name;
  const uint8_t* data;
  uint32_t compressed_size;
  uint32_t decompressed_size;
};

#ifdef USE_ESP_IDF_STUBS
// Test builds: no embedded data, empty arrays.
static const HaDiscoveryCategory ha_discovery_categories[] = {};
static const uint16_t ha_discovery_category_count = 0;
#else
// All categories - generated from ha_discovery/*.jsonl at build time.
extern const HaDiscoveryCategory ha_discovery_categories[];
extern const uint16_t ha_discovery_category_count;
#endif

}  // namespace geappliances_bridge
}  // namespace esphome
