/*
 * @file
 * @brief Unit tests for the erd_payload_formatter module.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#ifdef new
#undef new
#endif

#include "erd_payload_formatter.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

TEST_GROUP(erd_payload_formatter)
{
  void setup() {}
  void teardown() {}
};

TEST(erd_payload_formatter, format_binary_erd_value_as_hex_string)
{
  uint8_t value[] = {0xDE, 0xAD, 0xBE, 0xEF};
  std::string result = format_erd_payload(value, 4);
  CHECK_EQUAL(std::string("deadbeef"), result);
}

TEST(erd_payload_formatter, format_single_byte_as_hex)
{
  uint8_t value[] = {0x0F};
  std::string result = format_erd_payload(value, 1);
  CHECK_EQUAL(std::string("0f"), result);
}

TEST(erd_payload_formatter, format_string_erd_value_as_ascii_hex)
{
  // Even ASCII text is hex-encoded, not returned as-is
  uint8_t value[] = {'H', 'i'};
  std::string result = format_erd_payload(value, 2);
  CHECK_EQUAL(std::string("4869"), result);
}

TEST(erd_payload_formatter, format_zero_byte)
{
  uint8_t value[] = {0x00};
  std::string result = format_erd_payload(value, 1);
  CHECK_EQUAL(std::string("00"), result);
}

TEST(erd_payload_formatter, format_max_byte)
{
  uint8_t value[] = {0xFF};
  std::string result = format_erd_payload(value, 1);
  CHECK_EQUAL(std::string("ff"), result);
}

TEST(erd_payload_formatter, empty_value_handling)
{
  uint8_t value[] = {0x01, 0x02};
  std::string result = format_erd_payload(value, 0);
  CHECK_EQUAL(std::string(""), result);
}

TEST(erd_payload_formatter, null_value_handling)
{
  std::string result = format_erd_payload(nullptr, 0);
  CHECK_EQUAL(std::string(""), result);
}

TEST(erd_payload_formatter, large_value_clamping)
{
  // size is uint8_t so max is 255; format should handle the full range
  uint8_t value[256];
  for (int i = 0; i < 256; i++) {
    value[i] = (uint8_t)i;
  }
  std::string result = format_erd_payload(value, 255);
  // 255 bytes -> 510 hex characters
  CHECK_EQUAL(510, (int)result.size());
  // first byte is 0x00 -> "00"
  CHECK_EQUAL(std::string("00"), result.substr(0, 2));
  // last byte is 0xFE -> "fe"
  CHECK_EQUAL(std::string("fe"), result.substr(508, 2));
}

TEST(erd_payload_formatter, build_erd_topic)
{
  std::string topic = build_erd_topic("myDevice", 0x1001);
  CHECK_EQUAL(std::string("geappliances/myDevice/erd/0x1001/value"), topic);
}

TEST(erd_payload_formatter, build_erd_topic_with_zero_erd)
{
  std::string topic = build_erd_topic("dev", 0x0000);
  CHECK_EQUAL(std::string("geappliances/dev/erd/0x0000/value"), topic);
}

TEST(erd_payload_formatter, build_erd_topic_with_max_erd)
{
  std::string topic = build_erd_topic("x", 0xFFFF);
  CHECK_EQUAL(std::string("geappliances/x/erd/0xffff/value"), topic);
}
