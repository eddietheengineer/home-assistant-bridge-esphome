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

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(mqtt_bridge_polling, should_publish_mqtt_on_first_poll_when_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(mqtt_bridge_polling, should_not_republish_mqtt_when_polled_erd_data_is_unchanged_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));
}

TEST(mqtt_bridge_polling, should_republish_mqtt_when_polled_erd_data_changes_and_only_publish_on_change_is_enabled)
{
  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  nothing_should_happen();
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_request_read(0xC0, polled_erd);
  after(polling_interval);
  should_update_erd(polled_erd, uint8_t(0x02));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x02));
}

// A late response from a discovery-phase read that arrives after the state
// machine has already transitioned to polling (device responded slower than
// retry_delay). The ERD must be registered and added to the polling list.
TEST(mqtt_bridge_polling, should_register_and_poll_erd_whose_discovery_response_arrives_late_in_polling_state)
{
  enum { late_erd = 0x7b00 };

  given_that_the_bridge_has_entered_polling_state();

  // Cycle 1: polling timer fires and begins reading polled_erd
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // Late discovery response for late_erd arrives before polled_erd responds.
  // Bridge registers it, publishes its value, then immediately reads the
  // newly-registered ERD (now at the next position in the polling list).
  should_register_erd(late_erd);
  should_update_erd(late_erd, uint8_t(0xAB));
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));

  // polled_erd arrives next; erd_index is now at the end so no further read
  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: late_erd is now in the polling list alongside polled_erd
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // polled_erd completes and immediately triggers the read for late_erd
  should_update_erd(polled_erd, uint8_t(0x01));
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  should_update_erd(late_erd, uint8_t(0xAB));
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xAB));
}

// Same late-response scenario with only_publish_on_change enabled.
TEST(mqtt_bridge_polling, should_register_and_poll_late_erd_when_only_publish_on_change_is_enabled)
{
  enum { late_erd = 0x7b05 };

  given_that_the_bridge_has_entered_polling_state(true);

  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // New ERD: always published on first read; bridge immediately reads the
  // newly-registered ERD (appended to the polling list).
  should_register_erd(late_erd);
  should_update_erd(late_erd, uint8_t(0xCD));
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));

  should_update_erd(polled_erd, uint8_t(0x01));
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  // Cycle 2: both ERDs polled; values unchanged → neither is republished
  should_request_read(0xC0, polled_erd);
  after(polling_interval);

  // polled_erd same value → not published; late_erd read is triggered inline
  should_request_read(0xC0, late_erd);
  when_a_poll_read_completes(0xC0, polled_erd, uint8_t(0x01));

  nothing_should_happen();
  when_a_poll_read_completes(0xC0, late_erd, uint8_t(0xCD));
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

// When api_parsed_list is set, the bridge should poll the mandatory appliance API
// feature ERDs then go directly to polling the API-provided list.
TEST(mqtt_bridge_polling_api_list, should_skip_discovery_and_poll_api_list_directly)
{
  // Init sends broadcast (appliance type read)
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  // Appliance responds: api_parsed_list is set so bridge transitions to
  // state_add_appliance_api_feature_erds (mandatory). Skip that discovery phase
  // and state_polling entry (which registers api_list ERDs) under mock disabled.
  mock().disable();
  uint8_t appliance_type = 0x03; // refrigeration
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // Polling timer fires: erd_index left past polling_list_count after feature ERD
  // discovery, so cycle restarts from the first api_list ERD.
  should_request_read(0xC0, api_erd_1);
  after(polling_interval);

  // First poll completes: registers erd_1 (deferred registration), publishes,
  // immediately reads erd_2
  should_register_erd(api_erd_1);
  should_update_erd(api_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  // Second poll completes: registers erd_2, publishes, no more ERDs to read
  should_register_erd(api_erd_2);
  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));
}

// After a full polling cycle, the next cycle should restart from the first ERD.
TEST(mqtt_bridge_polling_api_list, should_restart_poll_cycle_on_polling_timer)
{
  // Init + appliance discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  // Skip feature ERD discovery and api_list registration under mock disabled.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // First poll cycle starts on polling timer
  should_request_read(0xC0, api_erd_1);
  after(polling_interval);

  // Complete first cycle — deferred registration on first read
  should_register_erd(api_erd_1);
  should_update_erd(api_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));

  should_register_erd(api_erd_2);
  should_update_erd(api_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, api_erd_2, uint8_t(0xBB));

  // Polling timer fires: restart from erd_1 (already registered, no register_erd)
  should_request_read(0xC0, api_erd_1);
  after(polling_interval);

  should_update_erd(api_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, api_erd_2);
  when_a_poll_read_completes(0xC0, api_erd_1, uint8_t(0xAA));
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
TEST(mqtt_bridge_polling_custom_erds, should_poll_custom_erds_alongside_api_parsed_list)
{
  // Init sends broadcast
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized_with_api_list_and_custom_erds();

  // Skip feature ERD discovery and state_polling entry (registrations happen under
  // mock disabled). api_list ERDs and custom ERDs are both registered in state_polling.
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // Polling timer fires: start first cycle (api_erd first, then custom ERDs)
  should_request_read(0xC0, api_erd);
  after(polling_interval);

  // api_erd completes: registers (deferred), publishes, immediately reads custom_erd_1
  should_register_erd(api_erd);
  should_update_erd(api_erd, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_1);
  when_a_poll_read_completes(0xC0, api_erd, uint8_t(0xAA));

  // custom_erd_1 completes: registers, publishes, immediately reads custom_erd_2
  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  // custom_erd_2 completes: registers, publishes, no more ERDs in cycle
  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));

  // Polling timer fires: restart cycle from api_erd (already registered)
  should_request_read(0xC0, api_erd);
  after(polling_interval);
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
  mock().enable();

  // Polling timer fires: erd_index >= polling_list_count, so cycle restarts from 0,
  // reading custom_erd_1 first.
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);

  // custom_erd_1 completes: registers (deferred), publishes, immediately reads custom_erd_2
  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xBB));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xBB));

  // custom_erd_2 completes: registers, publishes, no more ERDs in cycle
  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xCC));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xCC));
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

  // Real appliance-type response: bridge transitions to state_add_appliance_api_feature_erds.
  // Skip feature ERD discovery and state_polling entry (registrations under mock disabled).
  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // Polling timer fires: verify all reads target 0xC0 (not 0xFF), confirming the
  // host address was correctly captured from the genuine appliance-type response.
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));
}

// When the polling bridge is used only for custom ERDs alongside a subscription bridge
// (subscribe/auto mode), it is initialized with a pre-known host address and
// api_parsed_list = custom ERDs.  The bridge must NOT broadcast to 0xFF; it
// goes directly to state_polling and begins polling the custom ERDs immediately.
TEST(mqtt_bridge_polling_custom_erds, should_poll_only_custom_erds_when_used_alongside_subscribe_bridge)
{
  // During init, state_polling entry fires immediately (no 0xFF broadcast).
  // Custom ERDs are added via _no_register — no registration during init.
  mqtt_bridge_polling_init_at_address(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    &mqtt_client.interface,
    polling_interval,
    false,
    0xC0,     // pre-known host address — no 0xFF broadcast
    custom_list, 2);

  // Polling timer fires: start first cycle with custom ERDs
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);

  // custom_erd_1 read completes: registers (deferred), publishes, reads custom_erd_2
  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  // custom_erd_2 read completes: registers, publishes, cycle ends
  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // Polling timer fires: restart cycle from custom_erd_1 (already registered)
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);
}

// When the custom ERD bridge (init_at_address) loses contact with the appliance
// (appliance_lost_timer fires after 60 s of no read completions), it must resume
// polling at the pre-known address WITHOUT broadcasting to 0xFF.  This regression
// was triggered by transient GEA3 read failures (e.g. a WiFi/MQTT blip lasting
// ~1 minute) causing the appliance_lost_timer to expire.
TEST(mqtt_bridge_polling_custom_erds, should_resume_polling_at_known_address_after_appliance_lost)
{
  // Init: go directly to state_polling (no 0xFF broadcast).
  // ERDs are added via _no_register — no registration during init.
  mqtt_bridge_polling_init_at_address(
    &self,
    &timer_group.timer_group,
    &erd_client.interface,
    &mqtt_client.interface,
    polling_interval,
    false,
    0xC0,
    custom_list, 2);

  // Normal polling cycle so the appliance_lost_timer is running.
  // ERDs are added via _no_register, so first read triggers deferred registration.
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xAA));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xAA));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xBB));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xBB));

  // Simulate 60 s with no read completions (appliance_lost_timer expires).
  // The bridge must NOT broadcast to 0xFF — it should re-enter state_polling
  // at 0xC0 and start a new cycle immediately.
  mock().disable();
  after(60000);  // appliance_lost_timeout
  mock().enable();

  // Polling timer fires: confirm reads target 0xC0 (not 0xFF).
  // ERDs were re-added via _no_register after re-entry, so deferred registration again.
  should_request_read(0xC0, custom_erd_1);
  after(polling_interval);

  should_register_erd(custom_erd_1);
  should_update_erd(custom_erd_1, uint8_t(0xCC));
  should_request_read(0xC0, custom_erd_2);
  when_a_poll_read_completes(0xC0, custom_erd_1, uint8_t(0xCC));

  should_register_erd(custom_erd_2);
  should_update_erd(custom_erd_2, uint8_t(0xDD));
  when_a_poll_read_completes(0xC0, custom_erd_2, uint8_t(0xDD));
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
TEST(mqtt_bridge_polling_sequential, should_not_restart_cycle_mid_cycle_when_timer_fires)
{
  // Init + skip feature ERD discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // First polling timer fires (first cycle): starts reading erd_a
  should_request_read(0xC0, erd_a);
  after(polling_interval);

  // erd_a completes, registers (deferred), reads erd_b
  should_register_erd(erd_a);
  should_update_erd(erd_a, uint8_t(0x01));
  should_request_read(0xC0, erd_b);
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  // Polling timer fires again while erd_b is in-flight — should NOT restart
  // the cycle because not all ERDs have completed.  No new read should be sent.
  after(polling_interval);

  // erd_b completes, registers, reads erd_c
  should_register_erd(erd_b);
  should_update_erd(erd_b, uint8_t(0x02));
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  // erd_c completes, registers, cycle is now fully done
  should_register_erd(erd_c);
  should_update_erd(erd_c, uint8_t(0x03));
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Next polling timer fires: all ERDs completed, so restart from erd_a (already registered)
  should_request_read(0xC0, erd_a);
  after(polling_interval);
}

// When a read fails (all retries exhausted), the cycle should advance to the
// next ERD — a failed read counts as "completed" for cycle-tracking purposes.
TEST(mqtt_bridge_polling_sequential, should_advance_cycle_on_read_failed)
{
  // Init + skip feature ERD discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // First polling timer fires: starts reading erd_a
  should_request_read(0xC0, erd_a);
  after(polling_interval);

  // erd_a fails — should advance to erd_b (no registration on failure)
  should_request_read(0xC0, erd_b);
  trigger_read_failed(erd_a);

  // erd_b completes: registers (deferred), reads erd_c
  should_register_erd(erd_b);
  should_update_erd(erd_b, uint8_t(0x02));
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  // erd_c completes: registers, cycle is now fully done (1 failed + 2 succeeded = 3 total)
  should_register_erd(erd_c);
  should_update_erd(erd_c, uint8_t(0x03));
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Next polling timer fires: all ERDs completed (success or failure), restart
  should_request_read(0xC0, erd_a);
  after(polling_interval);
}

// Verify that reads are strictly sequential — each read is sent only after
// the previous one has completed (success or failure), never in parallel.
TEST(mqtt_bridge_polling_sequential, should_read_erds_sequentially_one_at_a_time)
{
  // Init + skip feature ERD discovery
  should_request_read(0xFF, 0x0008);
  when_the_bridge_is_initialized();

  mock().disable();
  uint8_t appliance_type = 0x03;
  trigger_read_completed(0xC0, 0x0008, &appliance_type, sizeof(appliance_type));
  after(retry_delay * applianceApiFeatureErdCount);
  mock().enable();

  // First polling timer fires: erd_a
  should_request_read(0xC0, erd_a);
  after(polling_interval);

  // erd_a completes: registers (deferred) → erd_b
  should_register_erd(erd_a);
  should_update_erd(erd_a, uint8_t(0x01));
  should_request_read(0xC0, erd_b);
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x01));

  // erd_b completes: registers → erd_c
  should_register_erd(erd_b);
  should_update_erd(erd_b, uint8_t(0x02));
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x02));

  // erd_c completes: registers → no more ERDs in cycle
  should_register_erd(erd_c);
  should_update_erd(erd_c, uint8_t(0x03));
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x03));

  // Polling timer fires: restart from erd_a (already registered, no register_erd)
  should_request_read(0xC0, erd_a);
  after(polling_interval);

  // Second cycle: erd_a → erd_b → erd_c (all already registered)
  should_update_erd(erd_a, uint8_t(0x04));
  should_request_read(0xC0, erd_b);
  when_a_poll_read_completes(0xC0, erd_a, uint8_t(0x04));

  should_update_erd(erd_b, uint8_t(0x05));
  should_request_read(0xC0, erd_c);
  when_a_poll_read_completes(0xC0, erd_b, uint8_t(0x05));

  should_update_erd(erd_c, uint8_t(0x06));
  when_a_poll_read_completes(0xC0, erd_c, uint8_t(0x06));
}
