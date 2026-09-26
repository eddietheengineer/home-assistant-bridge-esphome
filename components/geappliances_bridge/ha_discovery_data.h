/*!
 * Compressed Home Assistant MQTT Discovery entity definitions.
 *
 * Chunked compression: each category is split into small
 * independently-compressible chunks, allowing decompression with a small
 * buffer.
 *
 * Auto-generated from ha_discovery JSONL files — do not edit manually;
 * run scripts/compress_ha_discovery.py to regenerate.
 */

#ifndef HA_DISCOVERY_DATA_H
#define HA_DISCOVERY_DATA_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint32_t offset;
  uint16_t size;
} ha_discovery_chunk_t;

typedef struct {
  const char* name;
  const uint8_t* data;
  const ha_discovery_chunk_t* chunks;
  uint16_t num_chunks;
  uint16_t max_decompressed_chunk;
} ha_discovery_category_t;

extern const ha_discovery_chunk_t ha_discovery_chunk_common[];

extern const uint8_t ha_discovery_data_common[];

extern const ha_discovery_chunk_t ha_discovery_chunk_refrigeration[];

extern const uint8_t ha_discovery_data_refrigeration[];

extern const ha_discovery_chunk_t ha_discovery_chunk_laundry[];

extern const uint8_t ha_discovery_data_laundry[];

extern const ha_discovery_chunk_t ha_discovery_chunk_dishwasher[];

extern const uint8_t ha_discovery_data_dishwasher[];

extern const ha_discovery_chunk_t ha_discovery_chunk_waterheater[];

extern const uint8_t ha_discovery_data_waterheater[];

extern const ha_discovery_chunk_t ha_discovery_chunk_range[];

extern const uint8_t ha_discovery_data_range[];

extern const ha_discovery_chunk_t ha_discovery_chunk_airconditioning[];

extern const uint8_t ha_discovery_data_airconditioning[];

extern const ha_discovery_chunk_t ha_discovery_chunk_waterfilter[];

extern const uint8_t ha_discovery_data_waterfilter[];

extern const ha_discovery_chunk_t ha_discovery_chunk_smallappliance[];

extern const uint8_t ha_discovery_data_smallappliance[];

extern const ha_discovery_chunk_t ha_discovery_chunk_energy[];

extern const uint8_t ha_discovery_data_energy[];

extern const ha_discovery_category_t ha_discovery_categories[];

extern const uint16_t ha_discovery_category_count;

// FNV-1a hash of all discovery data; changes when discovery definitions
// are updated (change detection).
#define HA_DISCOVERY_DATA_HASH 0x38e37445u

#endif
