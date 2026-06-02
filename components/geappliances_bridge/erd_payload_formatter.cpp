#include "erd_payload_formatter.h"

namespace esphome {
namespace geappliances_bridge {

std::string build_erd_topic(const std::string& device_id, tiny_erd_t erd) {
  char buf[128];
  snprintf(buf, sizeof(buf), "geappliances/%s/erd/0x%04x/value", device_id.c_str(), erd);
  return std::string(buf);
}

// All ERD values are published as hex-encoded strings.
std::string format_erd_payload(const uint8_t* value, uint8_t size) {
  std::string payload;
  payload.reserve(size * 2);
  static const char hex_chars[] = "0123456789abcdef";
  for (uint8_t i = 0; i < size; i++) {
    payload += hex_chars[(value[i] >> 4) & 0x0f];
    payload += hex_chars[value[i] & 0x0f];
  }
  return payload;
}

}  // namespace geappliances_bridge
}  // namespace esphome
