// Auto-generated — do not edit.
// Compressed JSONL data for HA discovery, embedded in flash.
//
// DESIGN TRADE-OFF: All 10 appliance category JSONL files are embedded as
// compressed byte arrays (~421KB compressed from 2.4MB). This eliminates
// network dependency (no HTTP/TLS needed) but increases firmware size.
// On ESP32-C3 with 1.8MB flash, this uses ~23% of flash. For appliances
// that only need 1-2 categories, this is overkill. A future optimization
// could lazy-load categories from network on first use, caching in PSRAM
// if available, to reduce flash usage for single-appliance deployments.
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
