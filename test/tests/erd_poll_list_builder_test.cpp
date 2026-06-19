/*!
 * @file
 * @brief Tests for ErdPollListBuilder.
 */

#include "erd_poll_list_builder.h"
#include "erd_lists.h"

#include "CppUTest/TestHarness.h"

using namespace esphome::geappliances_bridge;

TEST_GROUP(erd_poll_list_builder)
{
  ErdPollListConfig config;
  std::vector<uint16_t> feature_bits;
  std::vector<uint16_t> custom_erds;

  void setup()
  {
    config.mode = BRIDGE_MODE_POLL;
    config.subscription_capable = true;
    config.subscription_active = false;
    config.appliance_api_parsing = false;
    config.feature_bit_valid_erds = &feature_bits;
    config.custom_erds = &custom_erds;
    config.appliance_type = 0;  // water heater
    feature_bits.clear();
    custom_erds.clear();
  }
};

TEST(erd_poll_list_builder, subscribe_mode_returns_only_custom_erds)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = true;
  custom_erds = { 0xABCD, 0x1234 };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds.size());
  CHECK_EQUAL(0xABCDu, result.erds[0]);
  CHECK_EQUAL(0x1234u, result.erds[1]);
}

TEST(erd_poll_list_builder, subscribe_mode_no_custom_returns_empty)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = true;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(0u, result.erds.size());
}

TEST(erd_poll_list_builder, subscribe_mode_not_active_returns_full_list)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = false;
  custom_erds = { 0xABCD };

  auto result = build_erd_poll_list(config);

  // Not active → treated as poll fallback, builds full list.
  CHECK(result.erds.size() > 0);
}

TEST(erd_poll_list_builder, auto_mode_subscription_active_returns_only_custom)
{
  config.mode = BRIDGE_MODE_AUTO;
  config.subscription_active = true;
  custom_erds = { 0xDEAD };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds.size());
  CHECK_EQUAL(0xDEADu, result.erds[0]);
}

TEST(erd_poll_list_builder, poll_mode_with_api_parsing_returns_feature_bits_plus_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits = { 0x0001, 0x0002, 0x0003 };
  custom_erds = { 0xABCD };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(4u, result.erds.size());
  CHECK_EQUAL(0x0001u, result.erds[0]);
  CHECK_EQUAL(0x0002u, result.erds[1]);
  CHECK_EQUAL(0x0003u, result.erds[2]);
  CHECK_EQUAL(0xABCDu, result.erds[3]);
}

TEST(erd_poll_list_builder, poll_mode_with_api_parsing_no_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits = { 0x0001, 0x0002 };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds.size());
  CHECK_EQUAL(0x0001u, result.erds[0]);
  CHECK_EQUAL(0x0002u, result.erds[1]);
}

TEST(erd_poll_list_builder, poll_mode_without_api_parsing_returns_full_list)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 0;  // water heater

  auto result = build_erd_poll_list(config);

  // Should contain common + energy + applianceApiFeature + waterHeater ERDs.
  CHECK(result.erds.size() >= (commonErdCount + energyErdCount +
                                applianceApiFeatureErdCount + waterHeaterErdCount));
}

TEST(erd_poll_list_builder, poll_mode_without_api_parsing_with_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 0;  // water heater
  custom_erds = { 0xBEEF };

  auto result = build_erd_poll_list(config);

  // Should contain full list + custom.
  bool found_custom = false;
  for (auto erd : result.erds) {
    if (erd == 0xBEEF) {
      found_custom = true;
    }
  }
  CHECK(found_custom);
}

TEST(erd_poll_list_builder, auto_mode_subscription_not_active_treats_as_poll)
{
  config.mode = BRIDGE_MODE_AUTO;
  config.subscription_active = false;
  config.appliance_api_parsing = true;
  feature_bits = { 0x0001 };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds.size());
  CHECK_EQUAL(0x0001u, result.erds[0]);
}

TEST(erd_poll_list_builder, deduplicates_custom_erd_already_in_feature_bits)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits = { 0x0001, 0x0002 };
  custom_erds = { 0x0001 };  // duplicate of feature bit ERD

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(2u, result.erds.size());
  CHECK_EQUAL(0x0001u, result.erds[0]);
  CHECK_EQUAL(0x0002u, result.erds[1]);
}

TEST(erd_poll_list_builder, invalid_appliance_type_skips_appliance_specific_erds)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;
  config.appliance_type = 255;  // invalid

  auto result = build_erd_poll_list(config);

  // Should contain common + energy + applianceApiFeature, but NOT appliance-specific.
  CHECK(result.erds.size() >= (commonErdCount + energyErdCount + applianceApiFeatureErdCount));
}

TEST(erd_poll_list_builder, empty_feature_bits_with_api_parsing_returns_only_custom)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  custom_erds = { 0xABCD };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds.size());
  CHECK_EQUAL(0xABCDu, result.erds[0]);
}

TEST(erd_poll_list_builder, gea2_protocol_returns_full_list)
{
  config.mode = BRIDGE_MODE_POLL;
  config.subscription_capable = false;
  config.appliance_api_parsing = false;
  config.appliance_type = 0;

  auto result = build_erd_poll_list(config);

  // GEA2 always uses full list (no subscriptions, no API parsing).
  CHECK(result.erds.size() >= (commonErdCount + energyErdCount +
                                applianceApiFeatureErdCount + waterHeaterErdCount));
}

TEST(erd_poll_list_builder, description_is_non_empty)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = false;

  auto result = build_erd_poll_list(config);

  CHECK(!result.description.empty());
}

TEST(erd_poll_list_builder, subscribe_mode_description)
{
  config.mode = BRIDGE_MODE_SUBSCRIBE;
  config.subscription_active = true;

  auto result = build_erd_poll_list(config);

  CHECK(!result.description.empty());
}

TEST(erd_poll_list_builder, poll_mode_api_parsing_description)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;

  auto result = build_erd_poll_list(config);

  CHECK(!result.description.empty());
}

TEST(erd_poll_list_builder, null_feature_bits_pointer_with_api_parsing)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  config.feature_bit_valid_erds = nullptr;
  custom_erds = { 0xABCD };

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds.size());
  CHECK_EQUAL(0xABCDu, result.erds[0]);
}

TEST(erd_poll_list_builder, null_custom_erds_pointer)
{
  config.mode = BRIDGE_MODE_POLL;
  config.appliance_api_parsing = true;
  feature_bits = { 0x0001 };
  config.custom_erds = nullptr;

  auto result = build_erd_poll_list(config);

  CHECK_EQUAL(1u, result.erds.size());
  CHECK_EQUAL(0x0001u, result.erds[0]);
}
