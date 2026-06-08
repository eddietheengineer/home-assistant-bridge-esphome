/*!
 * @file
 * @brief Tests for MQTT polling bridge change detection
 */

extern "C" {
#include "mqtt_bridge_polling.h"
}

#include "erd_lists.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/mqtt_client_double.hpp"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

TEST_GROUP(mqtt_bridge_polling)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,

    // Number of timer expirations needed to skip discovery states.
    // The discovery chain is: common → energy → appliance_api_feature → appliance.
    // After the first read_completed (which advances erd_index in state_add_common_erds),
    // commonErdCount-1 more expirations drain the rest of common, then energyErdCount
    // drains energy, then applianceApiFeatureErdCount drains the feature ERD state, and
    // waterHeaterErdCount drains the water heater appliance-specific ERDs.
    common_erds_remaining = commonErdCount - 1,
    discovery_timer_expirations = common_erds_remaining + energyErdCount + applianceApiFeatureErdCount + waterHeaterErdCount,

    polled_erd = 0x0001
  };

  mqtt_bridge_polling_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  void setup()
  {
    mock().strictOrder();

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    mqtt_client_double_init(&mqtt_client);
  }

  void teardown()
  {
    mock().disable();
    mqtt_bridge_polling_destroy(&self);
    mock().enable();
  }

  void when_the_bridge_is_initialized(bool only_publish_on_change = false)
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      only_publish_on_change);
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

  void given_that_the_bridge_has_entered_polling_state(bool only_publish_on_change = false)
  {
    mock().disable();
    when_the_bridge_is_initialized(only_publish_on_change);

    // Identify the appliance (type 0x00 = water heater, 64 ERDs)
    uint8_t appliance_type = 0x00;
    trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

    // Add polled_erd (0x0001) to the polling list via first common ERD read_completed
    uint8_t initial_value = 0x00;
    trigger_read_completed(0xC0, polled_erd, &initial_value, sizeof(initial_value));

    // Skip remaining discovery ERDs using timer expirations
    after(retry_delay * discovery_timer_expirations);

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

  void should_register_erd(tiny_erd_t erd)
  {
    mock()
      .expectOneCall("register_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd);
  }

  template <typename T>
  void should_update_erd(tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;

    mock()
      .expectOneCall("update_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("value", reinterpret_cast<const uint8_t*>(&_value), sizeof(_value));
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

TEST(mqtt_bridge_polling, should_always_publish_mqtt_when_only_publish_on_change_is_disabled)
{
  given_that_the_bridge_has_entered_polling_state();

  mock().disable();
  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
  mock().enable();
}

TEST(mqtt_bridge_polling, should_publish_mqtt_on_first_poll_when_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  mock().disable();
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
  mock().enable();
}

TEST(mqtt_bridge_polling, should_not_republish_mqtt_when_polled_erd_data_is_unchanged_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  mock().disable();
  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
  mock().enable();
}

TEST(mqtt_bridge_polling, should_republish_mqtt_when_polled_erd_data_changes_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  mock().disable();
  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_update_erd(polled_erd, uint8_t(0x02));
  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x02));
  mock().enable();
}

// A late response from a discovery-phase read that arrives after the state
// machine has already transitioned to polling (device responded slower than
// retry_delay). The ERD must be registered and added to the polling list.
TEST(mqtt_bridge_polling, should_register_and_poll_erd_whose_discovery_response_arrives_late_in_polling_state)
{
  enum { late_erd = 0x7b00 };

  given_that_the_bridge_has_entered_polling_state();

  mock().disable();
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  should_register_erd(late_erd);
  should_update_erd(late_erd, uint8_t(0xAB));
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));

  should_update_erd(polled_erd, uint8_t(0x01));
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_update_erd(late_erd, uint8_t(0xAB));
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));
  mock().enable();
}

// Same late-response scenario with only_publish_on_change enabled.
TEST(mqtt_bridge_polling, should_register_and_poll_late_erd_when_only_publish_on_change_is_enabled)
{
  enum { late_erd = 0x7b05 };

  given_that_the_bridge_has_entered_polling_state(true);

  mock().disable();
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  should_register_erd(late_erd);
  should_update_erd(late_erd, uint8_t(0xCD));
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));

  should_update_erd(polled_erd, uint8_t(0x01));
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  nothing_should_happen();
  should_request_read(0xC0, polled_erd);
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  nothing_should_happen();
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));
  mock().enable();
}

// ============================================================================
// Tests for appliance API-parsed polling list (api_parsed_list feature)
// ============================================================================

TEST_GROUP(mqtt_bridge_polling_api_list)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    api_erd_1 = 0x1000,
    api_erd_2 = 0x2000
  };

  mqtt_bridge_polling_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  const tiny_erd_t api_list[2] = {api_erd_1, api_erd_2};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    mqtt_client_double_init(&mqtt_client);
  }

  void teardown()
  {
    mock().disable();
    mqtt_bridge_polling_destroy(&self);
    mock().enable();
  }

  void when_the_bridge_is_initialized()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false);
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

  void should_register_erd(tiny_erd_t erd)
  {
    mock()
      .expectOneCall("register_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd);
  }

  template <typename T>
  void should_update_erd(tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    mock()
      .expectOneCall("update_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("value", reinterpret_cast<const uint8_t*>(&_value), sizeof(_value));
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
TEST(mqtt_bridge_polling_api_list, should_skip_discovery_and_poll_api_list_directly)
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
  after(retry_delay * applianceApiFeatureErdCount);
  // Probe phase: both api_list ERDs respond and are registered.
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  should_update_erd(api_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
  mock().enable();
}

// After a full polling cycle, the next cycle should restart from the first ERD.
TEST(mqtt_bridge_polling_api_list, should_restart_poll_cycle_on_polling_timer)
{
  // Init + appliance discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  // Skip feature ERD discovery and probe phase under mock disabled.
  // Both api_list ERDs respond during probe and are registered there.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  should_update_erd(api_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_update_erd(api_erd_2, uint8_t(0xBB));
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));

  should_update_erd(api_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_update_erd(api_erd_2, uint8_t(0xBB));
  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
  mock().enable();
}

// ERDs in api_parsed_list that do not respond during probe are still added to the
// polling list (via the _no_register path in state_polling entry) and lazily
// registered the first time they respond to a poll read.
TEST(mqtt_bridge_polling_api_list, should_lazily_register_erds_that_did_not_respond_during_probe)
{
  // api_list with 3 ERDs; the middle one (0x3000) will time out during probe.
  const tiny_erd_t api_list_3[3] = {api_erd_1, 0x3000, api_erd_2};

  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();
  self.api_parsed_list       = api_list_3;
  self.api_parsed_list_count = 3;

  // Feature-bit ERDs time out, then probe phase begins.
  // api_erd_1 responds, 0x3000 times out, api_erd_2 responds.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));  // registered immediately
  after(retry_delay);                                                        // 0x3000 probe times out
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));  // registered immediately
  mock().disable();

  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  should_request_read(0xC0, 0x3000);
  after(polling_interval);

  should_update_erd(api_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));

  should_register_erd(0x3000);
  should_update_erd(0x3000, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, 0x3000, uint8_t(0xCC));
  mock().enable();
}

// An ERD that the appliance explicitly rejects with "not_supported" during probe
// must never appear in the polling list — not even for lazy registration.
TEST(mqtt_bridge_polling_api_list, should_permanently_exclude_erds_rejected_as_not_supported_during_probe)
{
  // api_list with 3 ERDs; the middle one (0x3000) is explicitly rejected.
  const tiny_erd_t api_list_3[3] = {api_erd_1, 0x3000, api_erd_2};

  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();
  self.api_parsed_list       = api_list_3;
  self.api_parsed_list_count = 3;

  // Feature-bit ERDs time out, probe phase begins.
  // api_erd_1 responds, 0x3000 is explicitly rejected, api_erd_2 responds.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd_1, &probe_val, sizeof(probe_val));  // registered immediately
  trigger_read_failed_not_supported(0x3000);                                // permanently excluded
  trigger_read_completed(0xC0, api_erd_2, &probe_val, sizeof(probe_val));  // registered immediately
  mock().disable();

  should_request_read(0xC0, api_erd_1);
  should_request_read(0xC0, api_erd_2);
  after(polling_interval);

  should_update_erd(api_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
  mock().enable();
}

// ============================================================================
// Tests for user-configured custom ERD polling list (custom_erd_list feature)
// ============================================================================

TEST_GROUP(mqtt_bridge_polling_custom_erds)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    api_erd = 0x1000,
    custom_erd_1 = 0x7000,
    custom_erd_2 = 0x7001
  };

  mqtt_bridge_polling_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  const tiny_erd_t api_list[1] = {api_erd};
  const tiny_erd_t custom_list[2] = {custom_erd_1, custom_erd_2};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    mqtt_client_double_init(&mqtt_client);
  }

  void teardown()
  {
    mock().disable();
    mqtt_bridge_polling_destroy(&self);
    mock().enable();
  }

  void when_the_bridge_is_initialized_with_api_list_and_custom_erds()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false);
    self.api_parsed_list = api_list;
    self.api_parsed_list_count = 1;
    self.custom_erd_list = custom_list;
    self.custom_erd_list_count = 2;
  }

  void when_the_bridge_is_initialized_with_custom_erds_only()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false);
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

  void should_register_erd(tiny_erd_t erd)
  {
    mock()
      .expectOneCall("register_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd);
  }

  template <typename T>
  void should_update_erd(tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    mock()
      .expectOneCall("update_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("value", reinterpret_cast<const uint8_t*>(&_value), sizeof(_value));
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
TEST(mqtt_bridge_polling_custom_erds, should_poll_custom_erds_alongside_api_parsed_list)
{
  // Init sends broadcast
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized_with_api_list_and_custom_erds();

  // Feature-bit ERDs time out, then probe phase: api_erd responds and is registered.
  // custom ERDs are not in api_parsed_list so they are added later in state_polling.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, api_erd, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, api_erd);
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  should_update_erd(api_erd, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd, uint8_t(0xAA));

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  should_request_read(0xC0, api_erd);
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));

  should_update_erd(api_erd, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, api_erd, uint8_t(0xAA));

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  should_request_read(0xC0, api_erd);
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));
  mock().enable();
}

// Custom ERDs should be polled in every cycle when only custom_erds are configured
// (no api_parsed_list, going through discovery).
TEST(mqtt_bridge_polling_custom_erds, should_poll_custom_erds_in_discovery_mode)
{
  mock().disable();
  // Initialize with custom ERDs only (no api_parsed_list)
  when_the_bridge_is_initialized_with_custom_erds_only();

  // Appliance type received: transition to discovery states (water heater = type 0)
  uint8_t appliance_type = 0x00;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));

  // Skip through all discovery ERDs via timer expirations (no responses provided).
  // This transitions through state_add_common_erds → state_add_energy_erds →
  // state_add_appliance_api_feature_erds → state_add_appliance_erds → state_polling.
  // In state_polling entry, custom ERDs are registered while mock is disabled.
  // erd_index is left at waterHeaterErdCount (>= polling_list_count=2), so the
  // initial send_next_poll_read_request is a no-op.
  // The count commonErdCount + energyErdCount + applianceApiFeatureErdCount +
  // waterHeaterErdCount is the number of timer expirations needed to exhaust each
  // discovery state's ERD list and transition to the next, with one expiration per
  // ERD slot per state.
  after(retry_delay * (commonErdCount + energyErdCount + applianceApiFeatureErdCount + waterHeaterErdCount));
  mock().disable();

  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));
  mock().enable();
}

// A spurious read_completed for a non-0x0008 ERD arriving while the bridge is
// still in state_identify_appliance (e.g. from a concurrent read on the shared
// erd_client) must not cause a premature transition to state_polling with
// erd_host_address still set to the broadcast address (0xFF).  The bridge must
// wait for the genuine ERD 0x0008 response before polling begins.
TEST(mqtt_bridge_polling_custom_erds, should_ignore_spurious_read_completed_during_identification)
{
  // Init sends broadcast identification read.
  should_request_read(0xFF, 0x0008);
  mqtt_bridge_polling_init(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    &mqtt_client.interface,
    polling_interval,
    false);
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
  after(retry_delay * applianceApiFeatureErdCount);
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, custom_erd_1, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, custom_erd_2, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  should_update_erd(custom_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));
  mock().enable();
}

// When the polling bridge is used only for custom ERDs alongside a subscription bridge
// (subscribe/auto mode), it is initialized with a pre-known host address and
// api_parsed_list = custom ERDs.  The bridge must NOT broadcast to 0xFF; it
// On first init, state_identify_appliance transitions to state_probe_api_parsed_erds
// (Phase 2 verification) instead of going directly to state_polling.  After ERDs
// are verified they are registered during probe and polled without deferred
// registration thereafter.
TEST(mqtt_bridge_polling_custom_erds, should_poll_only_custom_erds_when_used_alongside_subscribe_bridge)
{
  // Phase 2: state_probe_api_parsed_erds entry sends read for custom_erd_1 immediately.
  should_request_read(0xC0, custom_erd_1);
  mqtt_bridge_polling_init_at_address(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    &mqtt_client.interface,
    polling_interval,
    false,
    0xC0,     // pre-known host address — no 0xFF broadcast
    custom_list, 2);

  // Phase 2: custom_erd_1 responds — registered and published immediately.
  // Bridge sends read for custom_erd_2.
  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  // Phase 2: custom_erd_2 responds — registered and published immediately.
  // No more ERDs — transition to state_polling.
  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  mock().disable();
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  should_update_erd(custom_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_update_erd(custom_erd_2, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  should_update_erd(custom_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_update_erd(custom_erd_2, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));
  mock().enable();
}

// When the custom ERD bridge (init_at_address) loses contact with the appliance
// (appliance_lost_timer fires after 60 s of no read completions), it must resume
// polling at the pre-known address WITHOUT broadcasting to 0xFF.  On re-entry
// after appliance_lost the probe phase is skipped (deferred for future spec)
// and ERDs are lazily re-registered on first read.
TEST(mqtt_bridge_polling_custom_erds, should_resume_polling_at_known_address_after_appliance_lost)
{
  // Phase 2: state_probe_api_parsed_erds entry sends read for custom_erd_1.
  should_request_read(0xC0, custom_erd_1);
  mqtt_bridge_polling_init_at_address(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    &mqtt_client.interface,
    polling_interval,
    false,
    0xC0,
    custom_list, 2);

  // Phase 2 probe: both custom ERDs respond and are registered immediately.
  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  mock().disable();
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  should_update_erd(custom_erd_1, uint8_t(0xAA));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_update_erd(custom_erd_2, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  after(60000);  // appliance_lost_timeout

  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  after(polling_interval);

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xCC));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xDD));
  should_request_read(0xC0, custom_erd_1);
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xDD));
  mock().enable();
}

// ============================================================================
// Tests for sequential polling — one read at a time, cycle restarts only
// when all ERDs have completed AND the polling timer has expired
// ============================================================================

TEST_GROUP(mqtt_bridge_polling_sequential)
{
  enum {
    retry_delay = 100,
    polling_interval = 1000,
    erd_a = 0x1001,
    erd_b = 0x1002,
    erd_c = 0x1003
  };

  mqtt_bridge_polling_t self;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;
  mqtt_client_double_t mqtt_client;

  const tiny_erd_t api_list[3] = {erd_a, erd_b, erd_c};

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
    mqtt_client_double_init(&mqtt_client);
  }

  void teardown()
  {
    mock().disable();
    mqtt_bridge_polling_destroy(&self);
    mock().enable();
  }

  void when_the_bridge_is_initialized()
  {
    mqtt_bridge_polling_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      &mqtt_client.interface,
      polling_interval,
      false);
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

  void should_register_erd(tiny_erd_t erd)
  {
    mock()
      .expectOneCall("register_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd);
  }

  template <typename T>
  void should_update_erd(tiny_erd_t erd, T value)
  {
    static T _value;
    _value = value;
    mock()
      .expectOneCall("update_erd")
      .onObject(&mqtt_client)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("value", reinterpret_cast<const uint8_t*>(&_value), sizeof(_value));
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
TEST(mqtt_bridge_polling_sequential, should_fire_all_reads_simultaneously_on_cycle_start)
{
  // Init + skip feature ERD discovery; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  // Probe phase: each ERD responds — registered immediately (mock disabled)
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  should_update_erd(erd_a, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  should_update_erd(erd_b, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  should_update_erd(erd_c, uint8_t(0x03));
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  should_update_erd(erd_a, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  should_update_erd(erd_b, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  should_update_erd(erd_c, uint8_t(0x03));
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));
  mock().enable();
}

// When a read fails (all retries exhausted), the cycle should advance to the
// next ERD — a failed read counts as "completed" for cycle-tracking purposes.
TEST(mqtt_bridge_polling_sequential, should_advance_cycle_on_read_failed)
{
  // Init + skip feature ERD discovery; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  // Probe phase: each ERD responds — registered immediately (mock disabled)
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  trigger_read_failed(erd_a);

  should_update_erd(erd_b, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  should_update_erd(erd_c, uint8_t(0x03));
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  trigger_read_failed(erd_a);

  should_update_erd(erd_b, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  should_update_erd(erd_c, uint8_t(0x03));
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));
  mock().enable();
}

// Verify that all reads in a cycle are fired simultaneously from the polling
// timer, rather than sequentially one at a time.
TEST(mqtt_bridge_polling_sequential, should_read_all_erds_simultaneously_each_cycle)
{
  // Init + skip feature ERD discovery; probe phase: all 3 api ERDs respond
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  // Probe phase: each ERD responds — registered immediately (mock disabled)
  uint8_t probe_val = 0x01;
  trigger_read_completed(0xC0, erd_a, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_b, &probe_val, sizeof(probe_val));
  trigger_read_completed(0xC0, erd_c, &probe_val, sizeof(probe_val));
  mock().disable();

  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  after(polling_interval);

  should_update_erd(erd_a, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  should_update_erd(erd_b, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  should_update_erd(erd_c, uint8_t(0x03));
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  should_update_erd(erd_a, uint8_t(0x04));
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x04));

  should_update_erd(erd_b, uint8_t(0x05));
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x05));

  should_update_erd(erd_c, uint8_t(0x06));
  should_request_read(0xC0, erd_a);
  should_request_read(0xC0, erd_b);
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x06));
  mock().enable();
}
