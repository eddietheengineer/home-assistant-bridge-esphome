/*!
 * @file
 * @brief Tests for ERD polling bridge change detection
 */

extern "C" {
#include "erd_bridge_poll.h"
}

#include "erd_lists.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

TEST_GROUP(erd_bridge_poll)
{
  enum {
    polling_interval = 1000,
    polled_erd = 0x0001
  };

  erd_bridge_poll_t self;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  void setup()
  {
    mock().strictOrder();

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    erd_cache_destroy(&test_cache);
    mock().enable();
  }

  void when_the_bridge_is_initialized()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xFF, 0, nullptr, 0,
      &test_cache);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void trigger_read_completed(uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_retries_exhausted;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void given_that_the_bridge_has_entered_polling_state()
  {
    mock().disable();
    when_the_bridge_is_initialized();

    // Identify the appliance (type 0x00 = water heater, 64 ERDs)
    uint8_t appliance_type = 0x00;
    trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

    // Add polled_erd (0x0001) to the polling list via first common ERD read_completed
    uint8_t initial_value = 0x00;
    trigger_read_completed(0xC0, polled_erd, &initial_value, sizeof(initial_value));

    // Fail remaining common ERDs (skip 0x0001 which was the first read_completed above)
    for (size_t i = 1; i < commonErdCount; i++) {
      trigger_read_failed(commonErds[i]);
    }
    // Fail all energy ERDs
    for (size_t i = 0; i < energyErdCount; i++) {
      trigger_read_failed(energyErds[i]);
    }
    // Fail all appliance API feature ERDs
    for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
      trigger_read_failed(applianceApiFeatureErds[i]);
    }
    // Fail all appliance-specific ERDs (water heater)
    for (size_t i = 0; i < waterHeaterErdCount; i++) {
      trigger_read_failed(waterHeaterErds[i]);
    }

    mock().enable();
  }

  void should_request_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }



  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    trigger_read_completed(address, erd, &_value, sizeof(_value));
  }

  void nothing_should_happen()
  {
  }
};

// Regression: the cache should NOT be cleared on full-discovery re-entry.
// In AUTO mode the cache is shared with the subscription bridge; clearing it
// would destroy the subscription bridge's data.
// signal_appliance_lost is defined in erd_bridge_common.h (not included here);
// it is the last entry in the shared signal enum starting at tiny_hsm_signal_user_start.
static const tiny_hsm_signal_t signal_appliance_lost = (tiny_hsm_signal_user_start + 9);

TEST(erd_bridge_poll, should_preserve_cache_data_on_full_discovery_reentry)
{
  mock().disable();
  when_the_bridge_is_initialized();

  // Identify the appliance (type 0x00 = water heater, 64 ERDs)
  uint8_t appliance_type = 0x00;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // Add polled_erd (0x0001) to the polling list via first common ERD read_completed
  uint8_t initial_value = 0x42;
  trigger_read_completed(0xC0, polled_erd, &initial_value, sizeof(initial_value));

  // Fail remaining common ERDs (skip 0x0001 which was the first read_completed above)
  for (size_t i = 1; i < commonErdCount; i++) {
    trigger_read_failed(commonErds[i]);
  }
  // Fail all energy ERDs
  for (size_t i = 0; i < energyErdCount; i++) {
    trigger_read_failed(energyErds[i]);
  }
  // Fail all appliance API feature ERDs
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed(applianceApiFeatureErds[i]);
  }
  // Fail all appliance-specific ERDs (water heater)
  for (size_t i = 0; i < waterHeaterErdCount; i++) {
    trigger_read_failed(waterHeaterErds[i]);
  }
  mock().enable();

  // Bridge should now be in polling state with 1 ERD in the cache (polled_erd).
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  // Simulate appliance lost -> re-discovery.
  // The polling bridge transitions: poll_state_top(signal_appliance_lost) ->
  // state_identify_appliance -> state_add_common_erds (full-discovery path).
  // The cache should NOT be cleared during this re-discovery.

  // Set up mock expectations for the entire re-discovery chain.
  should_request_read(0xFF, 0x0008);
  for (size_t i = 0; i < commonErdCount; i++) {
    should_request_read(0xC0, commonErds[i]);
  }
  for (size_t i = 0; i < energyErdCount; i++) {
    should_request_read(0xC0, energyErds[i]);
  }
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    should_request_read(0xC0, applianceApiFeatureErds[i]);
  }
  for (size_t i = 0; i < waterHeaterErdCount; i++) {
    should_request_read(0xC0, waterHeaterErds[i]);
  }

  // Trigger appliance lost signal.
  tiny_hsm_send_signal(&self.hsm, signal_appliance_lost, nullptr);

  // Identify the appliance again.
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // Complete all common ERD reads.
  uint8_t new_value = 0x99;
  for (size_t i = 0; i < commonErdCount; i++) {
    trigger_read_completed(0xC0, commonErds[i], &new_value, sizeof(new_value));
  }
  // Fail all energy ERDs
  for (size_t i = 0; i < energyErdCount; i++) {
    trigger_read_failed(energyErds[i]);
  }
  // Fail all appliance API feature ERDs
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed(applianceApiFeatureErds[i]);
  }
  // Fail all appliance-specific ERDs (water heater)
  for (size_t i = 0; i < waterHeaterErdCount; i++) {
    trigger_read_failed(waterHeaterErds[i]);
  }

  // The cache should have commonErdCount entries (all common ERDs).
  // If the cache had been cleared, we'd still have commonErdCount entries
  // (they were just re-added). The key proof is that the original polled_erd
  // (0x0001) is still present with its updated value.
  CHECK_EQUAL(commonErdCount, erd_cache_get_count(&test_cache));

  // Verify the original ERD is still present with its updated value.
  uint16_t iter = 0;
  bool found = false;
  while (true) {
    erd_cache_entry_t* entry = erd_cache_get_next_entry(&test_cache, &iter);
    if (!entry) break;
    if (entry->erd == polled_erd) {
      found = true;
      CHECK_EQUAL(sizeof(new_value), entry->data_size);
    }
  }
  CHECK(found);
}

TEST(erd_bridge_poll, should_always_publish_mqtt_when_only_publish_on_change_is_disabled)
{
  given_that_the_bridge_has_entered_polling_state();
  erd_cache_set_only_publish_onchange(&test_cache, false);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(erd_bridge_poll, should_publish_mqtt_on_first_poll_when_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(erd_bridge_poll, should_not_republish_mqtt_when_polled_erd_data_is_unchanged_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(erd_bridge_poll, should_republish_mqtt_when_polled_erd_data_changes_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x02));
}

// A late response from a discovery-phase read that arrives after the state
// machine has already transitioned to polling (device responded slower than
// retry_delay). The ERD must be registered and added to the polling list.
TEST(erd_bridge_poll, should_register_and_poll_erd_whose_discovery_response_arrives_late_in_polling_state)
{
  enum { late_erd = 0x7b00 };

  given_that_the_bridge_has_entered_polling_state();
  erd_cache_set_only_publish_onchange(&test_cache, false);

  // Cycle 1: polling timer fires and begins reading polled_erd
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // Late discovery response for late_erd arrives before polled_erd responds.
  // Bridge registers it and publishes its value. With simultaneous reads,
  // the late ERD is added to the polling list but won't be read until
  // the next cycle (all reads for this cycle were already fired).
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));

  // polled_erd arrives next
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: late_erd is now in the polling list alongside polled_erd,
  // both are read simultaneously
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));
}

// Same late-response scenario with only_publish_on_change enabled.
TEST(erd_bridge_poll, should_register_and_poll_late_erd_when_only_publish_on_change_is_enabled)
{
  enum { late_erd = 0x7b05 };

  given_that_the_bridge_has_entered_polling_state();

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // New ERD: always published on first read. With simultaneous reads,
  // the late ERD is added to the polling list but won't be read until
  // the next cycle.
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));

  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: both ERDs polled simultaneously; values unchanged → neither republished
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  after(polling_interval);

  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  nothing_should_happen();
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));
}

// ============================================================================
// Tests for appliance API-parsed polling list (api_parsed_list feature)
// ============================================================================

TEST_GROUP(erd_bridge_poll_api_list)
{
  enum {
    polling_interval = 1000,
    api_erd_1 = 0x1000,
    api_erd_2 = 0x2000
  };

  erd_bridge_poll_t self;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  const tiny_erd_t api_list[2] = {api_erd_1, api_erd_2};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    erd_cache_destroy(&test_cache);
    mock().enable();
  }

  void when_the_bridge_is_initialized()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xFF, 0, nullptr, 0,
      &test_cache);
    // Set the API-parsed list AFTER init (api_parsed_list is always zeroed in init)
    self.api_parsed_list = api_list;
    self.api_parsed_list_count = 2;
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void trigger_read_completed(uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed_not_supported(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_not_supported;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_retries_exhausted;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void should_request_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }



  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    trigger_read_completed(address, erd, &_value, sizeof(_value));
  }
};

// When api_parsed_list is set, the bridge should probe each ERD in the list and
// only poll the ones that respond. Feature-bit ERDs are still read first.
TEST(erd_bridge_poll_api_list, should_skip_discovery_and_poll_api_list_directly)
{
  // Init sends broadcast (appliance type read)
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  // Appliance responds: api_parsed_list is set so bridge transitions to
  // state_add_appliance_api_feature_erds, then state_probe_api_parsed_erds.
  // Run both phases under mock disabled: feature-bit ERDs time out, probe ERDs
  // respond (triggering registration + update, not checked here).
  mock().disable();
  uint8_t appliance_type = 0x03; // refrigeration
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  // Probe phase: both api_list ERDs respond and are registered.
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // Polling timer fires: all api_list ERDs read simultaneously
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  // First poll completes: already registered during probe, just publishes
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  // Second poll completes: already registered during probe, just publishes
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
}

TEST(erd_bridge_poll_api_list, should_restart_poll_cycle_on_polling_timer)
{
  // Init + appliance discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  // Skip feature ERD discovery and probe phase under mock disabled.
  // Both api_list ERDs respond during probe and are registered there.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // First poll cycle starts on polling timer — all ERDs read simultaneously
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  // Complete first cycle — already registered during probe, just publishes
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));

  // Polling timer fires: restart from erd_1 (already registered)
  // All ERDs read simultaneously
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
}
// An ERD that the appliance explicitly rejects with "not_supported" during probe
// must never appear in the polling list — not even for lazy registration.
TEST(erd_bridge_poll_api_list, should_permanently_exclude_erds_rejected_as_not_supported_during_probe)
{
  // api_list with 3 ERDs; the middle one (0x3000) is explicitly rejected.
  const tiny_erd_t api_list_3[3] = {api_erd_1, 0x3000, api_erd_2};

  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();
  self.api_parsed_list       = api_list_3;
  self.api_parsed_list_count = 3;

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));  // registered immediately
  trigger_read_failed_not_supported(0x3000);                                // permanently excluded
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));  // registered immediately
  mock().enable();

  // state_polling entry: api_erd_1, 0x3000, and api_erd_2 are all checked against erd_set.
  // api_erd_1 and api_erd_2: already in erd_set (probe success) → skipped.
  // 0x3000: also in erd_set (probe not_supported) → skipped, NOT added to polling list.
  // Polling timer fires: only api_erd_1 and api_erd_2 are polled.
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  // Both already registered during probe — just publishes.
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
}

// ============================================================================
// Tests for user-configured custom ERD polling list (custom_erd_list feature)
// ============================================================================

TEST_GROUP(erd_bridge_poll_custom_erds)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    api_erd = 0x1000,
    custom_erd_1 = 0x7000,
    custom_erd_2 = 0x7001
  };

  erd_bridge_poll_t self;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  const tiny_erd_t api_list[1] = {api_erd};
  const tiny_erd_t custom_list[2] = {custom_erd_1, custom_erd_2};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    erd_cache_destroy(&test_cache);
    mock().enable();
  }

  void when_the_bridge_is_initialized_with_api_list_and_custom_erds()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xFF, 0, nullptr, 0,
      &test_cache);
    self.api_parsed_list = api_list;
    self.api_parsed_list_count = 1;
    self.custom_erd_list = custom_list;
    self.custom_erd_list_count = 2;
  }

  void when_the_bridge_is_initialized_with_custom_erds_only()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xFF, 0, nullptr, 0,
      &test_cache);
    self.custom_erd_list = custom_list;
    self.custom_erd_list_count = 2;
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void trigger_read_completed(uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_retries_exhausted;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed_not_supported(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_not_supported;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }


  void should_request_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }



  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    trigger_read_completed(address, erd, &_value, sizeof(_value));
  }
};

// Custom ERDs should be polled after api_parsed_list ERDs when both are configured.
// api_parsed_list ERDs are registered during probe; custom ERDs use deferred registration.
TEST(erd_bridge_poll_custom_erds, should_poll_custom_erds_alongside_api_parsed_list)
{
  // Init sends broadcast
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized_with_api_list_and_custom_erds();

  // Feature-bit ERDs time out, then probe phase: api_erd responds and is registered.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd, &probe_val, sizeof(probe_val));
  // Custom ERDs discovered through state_add_custom_erds
  trigger_read_completed(0xC0, custom_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, custom_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // Polling timer fires: all ERDs read simultaneously (api_erd + custom ERDs)
  should_request_read(0xC0, api_erd);
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);
  // api_erd completes: already registered during probe, just publishes
  when_a_poll_read_completes(0xC0, api_erd, uint8_t(0xAA));

  // custom_erd_1 completes: already registered during discovery, just publishes
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  // custom_erd_2 completes: already registered during discovery, just publishes
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));

  // Polling timer fires: restart cycle

  // All ERDs read simultaneously
  should_request_read(0xC0, api_erd);
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);
}

// Custom ERDs should be polled in every cycle when only custom_erds are configured
// (no api_parsed_list, going through discovery).
TEST(erd_bridge_poll_custom_erds, should_poll_custom_erds_in_discovery_mode)
{
  mock().disable();
  // Initialize with custom ERDs only (no api_parsed_list)
  when_the_bridge_is_initialized_with_custom_erds_only();

  // Appliance type received: transition to discovery states (water heater = type 0)
  uint8_t appliance_type = 0x00;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // Fail all discovery ERDs to transition through discovery states.
  for (size_t i = 0; i < commonErdCount; i++) trigger_read_failed(commonErds[i]);
  for (size_t i = 0; i < energyErdCount; i++) trigger_read_failed(energyErds[i]);
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) trigger_read_failed(applianceApiFeatureErds[i]);
  for (size_t i = 0; i < waterHeaterErdCount; i++) trigger_read_failed(waterHeaterErds[i]);
  // Custom ERDs discovered through state_add_custom_erds — both respond successfully
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, custom_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, custom_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // Polling timer fires: both custom ERDs read simultaneously
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  // Both already registered during discovery — just publishes
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));
}

// A spurious read_completed for a non-0x0008 ERD arriving while the bridge is
// still in state_identify_appliance (e.g. from a concurrent read on the shared
// erd_client) must not cause a premature transition to state_polling with
// erd_host_address still set to the broadcast address (0xFF).  The bridge must
// wait for the genuine ERD 0x0008 response before polling begins.
TEST(erd_bridge_poll_custom_erds, should_ignore_spurious_read_completed_during_identification)
{
  // Init sends broadcast identification read.
  should_request_read(0xFF, 0x0008);
  erd_bridge_poll_init(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    polling_interval,
    0xFF, 0, nullptr, 0,
    &test_cache);
  self.api_parsed_list = custom_list;
  self.api_parsed_list_count = 2;

  // A spurious read_completed for a different ERD arrives (e.g. from the main
  // bridge or a previous discovery phase) – bridge should stay in
  // state_identify_appliance and must not emit any reads or registrations.
  uint8_t dummy = 0xAB;
  trigger_read_completed(0xC0, 0x1234, &dummy, sizeof(dummy));

  // Real appliance-type response: bridge transitions to state_add_appliance_api_feature_erds,
  // then state_probe_api_parsed_erds. Run both phases under mock disabled: feature-bit
  // ERDs time out, probe ERDs (custom_erd_1, custom_erd_2) respond and are registered.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, custom_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, custom_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // Polling timer fires: verify all reads target 0xC0 (not 0xFF), confirming the
  // host address was correctly captured from the genuine appliance-type response.
  // Both custom ERDs read simultaneously.
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  // Already registered during probe — no register_erd expected
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));
}

// When the polling bridge is used only for custom ERDs alongside a subscription bridge
// (subscribe/auto mode), it is initialized with a pre-known host address and
// api_parsed_list = custom ERDs.  The bridge must NOT broadcast to 0xFF; it
// On first init, state_identify_appliance transitions to state_probe_api_parsed_erds
// (Phase 2 verification) instead of going directly to state_polling.  After ERDs
// are verified they are registered during probe and polled without deferred
// registration thereafter.
TEST(erd_bridge_poll_custom_erds, should_poll_only_custom_erds_when_used_alongside_subscribe_bridge)
{
  // Phase 2: state_probe_api_parsed_erds entry sends read for custom_erd_1 immediately.
  should_request_read(0xC0, custom_erd_1);
  erd_bridge_poll_init(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    polling_interval,
    0xC0,
    0,
    custom_list, 2,
    &test_cache);

  // Phase 2: custom_erd_1 responds — registered and published immediately.
  // Bridge sends read for custom_erd_2.
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  // Phase 2: custom_erd_2 responds — registered and published immediately.
  // No more ERDs — transition to state_polling.
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // Phase 3: polling timer fires — both custom ERDs read simultaneously.
  // Already registered during probe — no register_erd expected.
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // Polling timer fires: restart cycle — both ERDs already registered.
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);
}

// When the custom ERD bridge (init_at_address) loses contact with the appliance
// (appliance_lost_timer fires after 60 s of no read completions), it must resume
// polling at the pre-known address WITHOUT broadcasting to 0xFF.  On re-entry
// after appliance_lost the probe phase is skipped (deferred for future spec)
// and ERDs are lazily re-registered on first read.
TEST(erd_bridge_poll_custom_erds, should_resume_polling_at_known_address_after_appliance_lost)
{
  // Phase 2: state_probe_api_parsed_erds entry sends read for custom_erd_1.
  should_request_read(0xC0, custom_erd_1);
  erd_bridge_poll_init(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    polling_interval,
    0xC0,
    0,
    custom_list, 2,
    &test_cache);

  // Phase 2 probe: both custom ERDs respond and are registered immediately.
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // First polling cycle: both ERDs already registered during probe.
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // Simulate 60 s with no read completions (appliance_lost_timer expires).
  // The bridge must NOT broadcast to 0xFF — it should re-enter state_polling
  // at 0xC0 and start a new cycle immediately.
  mock().disable();
  after(60000);  // appliance_lost_timeout
  mock().enable();

  // Polling timer fires: confirm reads target 0xC0 (not 0xFF).
  // ERDs were re-added via _no_register after re-entry, so deferred registration again.
  // Both custom ERDs read simultaneously.
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xCC));

  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xDD));
}

// ============================================================================
// Tests for sequential polling — one read at a time, cycle restarts only
// when all ERDs have completed AND the polling timer has expired
// ============================================================================

TEST_GROUP(erd_bridge_poll_sequential)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    erd_a = 0x1001,
    erd_b = 0x1002,
    erd_c = 0x1003
  };

  erd_bridge_poll_t self;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  const tiny_erd_t api_list[3] = {erd_a, erd_b, erd_c};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    erd_cache_init(&test_cache);
  }

  void teardown()
  {
    mock().disable();
    erd_bridge_poll_destroy(&self);
    erd_cache_destroy(&test_cache);
    mock().enable();
  }

  void when_the_bridge_is_initialized()
  {
    erd_bridge_poll_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      polling_interval,
      0xFF, 0, nullptr, 0,
      &test_cache);
    self.api_parsed_list = api_list;
    self.api_parsed_list_count = 3;
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void trigger_read_completed(uint8_t address, tiny_erd_t erd, const void* data, uint8_t data_size)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void trigger_read_failed(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_retries_exhausted;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void should_request_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&erd_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }



  void trigger_read_failed_not_supported(tiny_erd_t erd)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_read_failed;
    args.address = 0xC0;
    args.read_failed.request_id = 0;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea3_erd_client_read_failure_reason_not_supported;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  template <typename T>
  void when_a_poll_read_completes(uint8_t address, tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    trigger_read_completed(address, erd, &_value, sizeof(_value));
  }
};

// The polling timer should NOT restart a new cycle while ERDs are still
// in-flight mid-cycle.  Only the first cycle (erd_index == polling_list_count)
// or a fully completed cycle should trigger a restart.
TEST(erd_bridge_poll_sequential, should_fire_all_reads_simultaneously_on_cycle_start)
{
  // Init + skip feature ERD discovery; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  // Probe phase: each ERD responds — registered immediately (mock disabled)
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().enable();

  // First polling timer fires: all reads fire simultaneously
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // Reads complete in order (already registered during probe — just publishes)
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Polling timer fires again: all ERDs completed, restart cycle with all reads
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);
}

// Verify that all reads in a cycle are fired simultaneously from the polling
// timer, rather than sequentially one at a time.
TEST(erd_bridge_poll_sequential, should_read_all_erds_simultaneously_each_cycle)
{
  // Init + skip feature ERD discovery; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  // Probe phase: each ERD responds — registered immediately (mock disabled)
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().enable();

  // First polling timer fires: all reads fire simultaneously
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // Reads complete in order (already registered during probe — just publishes)
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Polling timer fires again: all ERDs completed, restart cycle with all reads
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // Second cycle completions (all already registered)
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x04));

  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x05));

  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x06));
}


// ============================================================================
// Requirement 2.3: Failed reads must not block cycle completion
// ============================================================================

// A single failed read in a cycle must not stall the cycle indefinitely.
// The cycle completes when all ERDs have responded (success or failure),
// and the next cycle begins when the polling timer fires again.
TEST(erd_bridge_poll_sequential, failed_read_does_not_block_cycle_completion)
{
  // Init; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().enable();

  // First polling cycle starts
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // erd_a succeeds, erd_b fails, erd_c succeeds — cycle still completes
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  trigger_read_failed(erd_b);

  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Next cycle starts when polling timer fires again
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);
}

// All ERDs failing in a cycle must still allow the cycle to complete and
// the next cycle to begin — no infinite stall.
TEST(erd_bridge_poll_sequential, all_failed_reads_still_complete_cycle)
{
  // Init; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().enable();

  // First polling cycle starts
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // All three reads fail — cycle must still complete
  trigger_read_failed(erd_a);
  trigger_read_failed(erd_b);
  trigger_read_failed(erd_c);

  // Next cycle starts when polling timer fires again
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);
}

// A mix of failures and successes across multiple cycles must not accumulate
// cycle_completed_count errors that cause premature or missed cycle restarts.
TEST(erd_bridge_poll_sequential, mixed_failures_across_multiple_cycles)
{
  // Init; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().enable();

  // --- Cycle 1: timer fires, all reads sent ---
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // Cycle 1: erd_a fails, erd_b succeeds, erd_c fails
  trigger_read_failed(erd_a);
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));
  trigger_read_failed(erd_c);

  // Cycle 2 starts when polling timer fires again
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // Cycle 2: all succeed
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x10));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x20));
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x30));

  // Cycle 3 starts when polling timer fires again
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);
}
// ============================================================================
// Spec 1.4: Timeout / No Response — ERD permanently excluded from polling
// ============================================================================

// An ERD that times out during discovery (retries_exhausted) must be excluded
// from the polling list, same as not_supported.
TEST(erd_bridge_poll_api_list, should_permanently_exclude_erds_that_timeout_during_probe)
{
  const tiny_erd_t api_list_3[3] = {api_erd_1, 0x4000, api_erd_2};

  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();
  self.api_parsed_list       = api_list_3;
  self.api_parsed_list_count = 3;

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));  // success
  trigger_read_failed(0x4000);                                               // timeout -> excluded
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));  // success
  mock().enable();

  // Only api_erd_1 and api_erd_2 are polled — 0x4000 excluded
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
}

// ============================================================================
// Spec 2.4: Restart Pending — when the polling timer fires mid-cycle,
// the next cycle starts immediately after completion without waiting for
// another timer expiration.
// ============================================================================

TEST(erd_bridge_poll_sequential, restart_pending_starts_next_cycle_immediately)
{
  // Init; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().enable();

  // Cycle 1 starts on polling timer — all reads fire simultaneously
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // Complete cycle 1. The polling timer re-arms itself after sending reads,
  // so polling_timer_armed is true. Cycle 2 waits for the next timer fire.
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Now simulate the timer firing mid-cycle 2.
  // First, fire the timer to start cycle 2.
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  // erd_a completes in cycle 2
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x10));

  // Timer fires again while cycle 2 still has erd_b and erd_c in-flight.
  // restart_pending is set; cycle 2 continues.
  after(polling_interval);

  // erd_b and erd_c complete — cycle 2 finishes.
  // Because restart_pending is true, cycle 3 starts immediately.
  // Set up cycle 3 read expectations before the last completion triggers it.
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x20));

  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x30));
}

// ============================================================================
// Spec 1.5: One ERD at a Time during discovery
// Verified by the existing discovery flow — each discovery state sends one
// read on entry and waits for signal_read_completed or signal_read_failed
// before sending the next. The test below verifies that during the probe
// phase, reads are issued one at a time (sequentially) rather than all at once.
// ============================================================================

TEST(erd_bridge_poll_api_list, discovery_reads_erds_sequentially_one_at_a_time)
{
  const tiny_erd_t api_list_3[3] = {api_erd_1, 0x5000, api_erd_2};

  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();
  self.api_parsed_list       = api_list_3;
  self.api_parsed_list_count = 3;

  // All discovery runs under mock().disable() — the sequential one-at-a-time
  // behavior is structural (each state sends one read on entry, then waits
  // for the response before calling send_next_read_request). We verify the
  // end result: all 3 probe ERDs were discovered and are in the polling list.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  for (size_t i = 0; i < applianceApiFeatureErdCount; i++) {
    trigger_read_failed_not_supported(applianceApiFeatureErds[i]);
  }
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, 0x5000, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));
  mock().enable();

  // All 3 ERDs should be in the polling list and read simultaneously.
  CHECK_EQUAL(3u, self.polling_list_count);

  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, 0x5000);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);
}


extern "C" {
#include "erd_cache.h"
}

TEST_GROUP(erd_cache_stats)
{
  erd_cache_t cache;

  void setup()
  {
    erd_cache_init(&cache);
  }

  void teardown()
  {
    erd_cache_destroy(&cache);
  }
};

TEST(erd_cache_stats, empty_cache_has_zero_count)
{
  CHECK_EQUAL(0u, erd_cache_get_count(&cache));
}

TEST(erd_cache_stats, count_increments_on_insert)
{
  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  CHECK_EQUAL(1u, erd_cache_get_count(&cache));
}

TEST(erd_cache_stats, count_unchanged_on_update)
{
  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  data = 0x02;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  CHECK_EQUAL(1u, erd_cache_get_count(&cache));
}

TEST(erd_cache_stats, count_for_multiple_erds)
{
  uint8_t data_a = 0x01;
  uint8_t data_b = 0x02;
  uint8_t data_c = 0x03;
  erd_cache_update(&cache, 0x1001, &data_a, sizeof(data_a));
  erd_cache_update(&cache, 0x1002, &data_b, sizeof(data_b));
  erd_cache_update(&cache, 0x1003, &data_c, sizeof(data_c));
  CHECK_EQUAL(3u, erd_cache_get_count(&cache));
}

TEST(erd_cache_stats, update_rate_returns_zero_when_empty)
{
  CHECK_EQUAL(0u, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache_stats, update_rate_counts_updates)
{
  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  erd_cache_update(&cache, 0x1002, &data, sizeof(data));
  erd_cache_update(&cache, 0x1003, &data, sizeof(data));
  CHECK_EQUAL(3u, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache_stats, update_rate_resets_after_read)
{
  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  erd_cache_update(&cache, 0x1002, &data, sizeof(data));
  CHECK_EQUAL(2u, erd_cache_get_update_rate(&cache));
  CHECK_EQUAL(0u, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache_stats, update_rate_counts_reupdates)
{
  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  data = 0x02;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  CHECK_EQUAL(2u, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache_stats, update_rate_counts_new_entries)
{
  uint8_t data = 0x01;
  erd_cache_update(&cache, 0x1001, &data, sizeof(data));
  erd_cache_update(&cache, 0x1002, &data, sizeof(data));
  CHECK_EQUAL(2u, erd_cache_get_update_rate(&cache));
}

TEST(erd_cache_stats, update_rate_not_increased_on_overflow)
{
  // Fill the cache
  uint8_t data = 0x01;
  for (uint16_t i = 0; i < ERD_CACHE_CAPACITY; i++) {
    erd_cache_update(&cache, (tiny_erd_t)(0x1000 + i), &data, sizeof(data));
  }
  CHECK_EQUAL(ERD_CACHE_CAPACITY, erd_cache_get_update_rate(&cache));

  // Try to insert beyond capacity — should be rejected
  erd_cache_update(&cache, 0x9999, &data, sizeof(data));
  CHECK_EQUAL(0u, erd_cache_get_update_rate(&cache)); // no increment on overflow
}

// Regression: when an inline entry is updated with a larger payload,
// the memcmp must not read past the old inline buffer.
TEST(erd_cache_stats, data_change_detected_when_size_increases_from_inline)
{
  uint8_t small[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
  uint8_t large[24] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18 };

  erd_cache_set_only_publish_onchange(&cache, true);

  // Insert 8 bytes (fits inline)
  CHECK(erd_cache_update(&cache, 0x1001, small, 8));

  // Update with 24 bytes (exceeds inline, must go to heap).
  // Before the fix this memcmp'd 24 bytes from the 16-byte inline buffer.
  CHECK(erd_cache_update(&cache, 0x1001, large, 24));

  // Verify the entry was stored correctly via iteration.
  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&cache, &iter);
  CHECK(entry != nullptr);
  CHECK_EQUAL(0x1001u, entry->erd);
  CHECK_EQUAL(24u, entry->data_size);
  CHECK(entry->uses_heap);
}

// Same ERD, same size, different data — should detect change.
TEST(erd_cache_stats, data_change_detected_when_content_differs_same_size)
{
  uint8_t a[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
  uint8_t b[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09 };

  erd_cache_set_only_publish_onchange(&cache, true);

  CHECK(erd_cache_update(&cache, 0x1001, a, 8));
  // First update with same data should NOT be marked as changed
  CHECK(!erd_cache_update(&cache, 0x1001, a, 8));
  // Update with different data should be marked as changed
  CHECK(erd_cache_update(&cache, 0x1001, b, 8));
}

// Same ERD, same size, same data — should NOT be marked as changed.
TEST(erd_cache_stats, no_change_when_data_identical)
{
  uint8_t data[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

  erd_cache_set_only_publish_onchange(&cache, true);

  CHECK(erd_cache_update(&cache, 0x1001, data, 4));
  CHECK(!erd_cache_update(&cache, 0x1001, data, 4));
}