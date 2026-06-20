/*!
 * @file
 * @brief ESP-IDF esp_zlib stub for test/simulation builds.
 */

#ifndef ESP_ZLIB_STUB_H
#define ESP_ZLIB_STUB_H

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL 1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Stub: always fails (no real zlib in test builds).
 * The test build's publish_ha_discovery_() is a no-op anyway.
 */
static inline esp_err_t esp_zlib_inflate(const uint8_t* in, uint32_t in_len,
                                          uint8_t* out, uint32_t* out_len)
{
  (void)in; (void)in_len; (void)out; (void)out_len;
  return ESP_FAIL;
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_ZLIB_STUB_H */
