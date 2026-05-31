#include "erd_payload_formatter.h"

#include <cctype>

namespace esphome {
namespace geappliances_bridge {

std::string build_erd_topic(const std::string& device_id, tiny_erd_t erd) {
  char buf[64];
  snprintf(buf, sizeof(buf), "geappliances/%s/erd/0x%04x/value", device_id.c_str(), erd);
  return std::string(buf);
}

std::string format_erd_payload(tiny_erd_t erd,
                               const uint8_t* value,
                               uint8_t size,
                               ErdRegistry* registry) {
  bool is_string = (registry != nullptr && registry->is_string_type(erd));

  if (is_string) {
    // String-type ERDs: publish as ASCII (stop at null, skip non-printable)
    std::string payload;
    uint8_t str_len = 0;
    while (str_len < size && value[str_len] != 0) str_len++;
    payload.reserve(str_len);
    for (uint8_t i = 0; i < str_len; i++) {
      if (isprint(value[i])) {
        payload += static_cast<char>(value[i]);
      }
    }
    return payload;
  }

  // Binary ERDs: hex encoding
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
