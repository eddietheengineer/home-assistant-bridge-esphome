/*!
 * @file
 * @brief
 */

extern "C" {
#include "erd_cache.h"
}

#include "erd_bridge_subscribe.h"

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"

TEST_GROUP(erd_bridge_subscribe)
{
  enum {
    resubscribe_delay = 1000,
    subscription_retention_period = 30 * 1000
  };

  erd_bridge_subscribe_t self;
  erd_cache_t test_cache;

  tiny_timer_group_double_t timer_group;
  tiny_gea3_erd_client_double_t erd_client;

  void setup()
  {
    mock().strictOrder();

    tiny_timer_group_double_init(&timer_group);
    tiny_gea3_erd_client_double_init(&erd_client);
  }

  void teardown()
  {
    erd_bridge_subscribe_destroy(&self);
    erd_cache_destroy(&test_cache);
  }

  void when_the_bridge_is_initialized(uint8_t address = 0xC0)
  {
    erd_bridge_subscribe_init(
      &self,
      &timer_group.timer_group,
      &erd_client.interface,
      address,
      &test_cache);
  }

  void given_that_the_bridge_has_been_initialized()
  {
    mock().disable();
    when_the_bridge_is_initialized();
    mock().enable();
  }

  void after_a_subscription_is_added_or_retained_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = address;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void given_that_a_subscription_has_been_added_or_retained_successfully_for(uint8_t address)
  {
    mock().disable();
    after_a_subscription_is_added_or_retained_for(address);
    mock().enable();
  }

  void given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(uint8_t address)
  {
    given_that_the_bridge_has_been_initialized();
    given_that_a_subscription_has_been_added_or_retained_successfully_for(address);
  }

  void a_subscription_to_should_be_requested_for(uint8_t address)
  {
    mock()
      .expectOneCall("subscribe")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(true);
  }

  void a_subscription_should_be_requested_and_will_fail_to_queue_for(uint8_t address)
  {
    mock()
      .expectOneCall("subscribe")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(false);
  }

  void a_subscription_retention_should_be_requested_for(uint8_t address)
  {
    mock()
      .expectOneCall("retain_subscription")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(true);
  }


  template <typename T>
  void when_an_erd_publication_is_received(uint8_t publisher_address, tiny_erd_t erd, T data)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = publisher_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = &data;
    args.subscription_publication_received.data_size = sizeof(data);

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  template <typename T>
  void given_that_an_erd_publication_has_been_received(uint8_t publisher_address, tiny_erd_t erd, T data)
  {
    mock().disable();
    when_an_erd_publication_is_received(publisher_address, erd, data);
    mock().enable();
  }

  void when_a_subscription_host_came_online_is_received_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_host_came_online;
    args.address = address;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void when_a_subscribe_failure_is_received_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscribe_failed;
    args.address = address;

    tiny_gea3_erd_client_double_trigger_activity_event(
      &erd_client,
      &args);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void nothing_should_happen()
  {
  }
};

TEST(erd_bridge_subscribe, should_subscribe_when_initialized)
{
  a_subscription_to_should_be_requested_for(0xC0);
  when_the_bridge_is_initialized();
}

TEST(erd_bridge_subscribe, should_retry_subscribe_after_a_delay_if_the_subscribe_request_fails_to_queue)
{
  a_subscription_should_be_requested_and_will_fail_to_queue_for(0xC0);
  when_the_bridge_is_initialized();

  nothing_should_happen();
  after(resubscribe_delay - 1);

  a_subscription_should_be_requested_and_will_fail_to_queue_for(0xC0);
  after(1);

  a_subscription_to_should_be_requested_for(0xC0);
  after(resubscribe_delay);
}

TEST(erd_bridge_subscribe, should_retry_subscribe_if_the_subscribe_request_fails)
{
  given_that_the_bridge_has_been_initialized();
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscribe_failure_is_received_for(0xC0);
}

TEST(erd_bridge_subscribe, should_not_retry_subscribe_if_the_subscribe_request_fails_for_a_different_address)
{
  given_that_the_bridge_has_been_initialized();
  nothing_should_happen();
  when_a_subscribe_failure_is_received_for(0xC1);
}

TEST(erd_bridge_subscribe, should_resubscribe_after_receiving_a_subscription_host_came_online_from_the_erd_host)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscription_host_came_online_is_received_for(0xC0);
}

TEST(erd_bridge_subscribe, should_ignore_subscription_host_came_online_from_other_addresses)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  nothing_should_happen();
  when_a_subscription_host_came_online_is_received_for(0xC1);
}

// Regression: the cache should NOT be cleared on host-came-online.
// In AUTO mode the cache is shared with the polling bridge; clearing it
// would destroy the polling bridge's data.
TEST(erd_bridge_subscribe, should_preserve_cache_data_on_host_came_online)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  // Populate the cache with an ERD value via a publication.
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  // Simulate the appliance host restarting.
  a_subscription_to_should_be_requested_for(0xC0);
  when_a_subscription_host_came_online_is_received_for(0xC0);

  // The cache entry should still be present (not cleared).
  CHECK_EQUAL(1u, erd_cache_get_count(&test_cache));

  uint16_t iter = 0;
  erd_cache_entry_t* entry = erd_cache_get_next_entry(&test_cache, &iter);
  CHECK(entry != nullptr);
  CHECK_EQUAL(0xABCDu, entry->erd);
}

TEST(erd_bridge_subscribe, should_ignore_subscription_added_activity_for_other_addresses)
{
  given_that_the_bridge_has_been_initialized();
  nothing_should_happen();
  after_a_subscription_is_added_or_retained_for(0xC1);
  after(subscription_retention_period);
}

TEST(erd_bridge_subscribe, should_periodically_retain_an_active_subscription)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);

  nothing_should_happen();
  after(subscription_retention_period - 1);

  a_subscription_retention_should_be_requested_for(0xC0);
  after(1);
}

TEST(erd_bridge_subscribe, should_register_and_update_newly_discovered_erds_when_published_by_the_erd_client)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  when_an_erd_publication_is_received(0xC0, 0xABCD, uint32_t(0x12345678));
}

TEST(erd_bridge_subscribe, should_update_known_erds_when_published_by_the_erd_client)
{
  given_that_the_bridge_has_been_initialized_and_a_subscription_is_active_for(0xC0);
  given_that_an_erd_publication_has_been_received(0xC0, 0xABCD, uint32_t(0x12345678));
  when_an_erd_publication_is_received(0xC0, 0xABCD, uint32_t(0x87654321));
}

// This makes sure that if we miss the ERD subscription added message that we still handle ERD publications
// Since the ERD client acknowledges publications even if a subscription isn't known to be active, this is
// necessary to make sure that we don't miss any ERD publications
TEST(erd_bridge_subscribe, should_handle_erd_publications_even_when_a_subscription_is_not_confirmed_active)
{
  given_that_the_bridge_has_been_initialized();
  when_an_erd_publication_is_received(0xC0, 0xABCD, uint32_t(0x12345678));
}
TEST(erd_bridge_subscribe, should_ignore_erd_publications_from_other_hosts)
{
  given_that_the_bridge_has_been_initialized();
  nothing_should_happen();
  when_an_erd_publication_is_received(0xC1, 0xABCD, uint32_t(0x12345678));
}

// Regression: destroy should not crash when erd_client is null.
TEST(erd_bridge_subscribe, should_not_crash_on_destroy_with_null_erd_client)
{
  erd_bridge_subscribe_t unsubscribed;
  memset(&unsubscribed, 0, sizeof(unsubscribed));
  // timer_group is null so the guard returns early; this should not crash.
  erd_bridge_subscribe_destroy(&unsubscribed);
}

// ---------------------------------------------------------------------------
// Dual-subscription tests: two independent bridge instances, each watching a
// different appliance address and publishing to its own MQTT client.
// ---------------------------------------------------------------------------

TEST_GROUP(erd_bridge_subscribe_dual)
{
  enum {
    address_a = 0xC0,
    address_b = 0xC4,
    resubscribe_delay = 1000,
    subscription_retention_period = 30 * 1000
  };

  erd_bridge_subscribe_t bridge_a;
  erd_bridge_subscribe_t bridge_b;
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
    erd_bridge_subscribe_destroy(&bridge_a);
    erd_bridge_subscribe_destroy(&bridge_b);
    erd_cache_destroy(&test_cache);
  }

  void given_both_bridges_are_initialized()
  {
    mock().disable();
    erd_bridge_subscribe_init(
      &bridge_a,
      &timer_group.timer_group,
      &erd_client.interface,
      address_a,
      &test_cache);
    erd_bridge_subscribe_init(
      &bridge_b,
      &timer_group.timer_group,
      &erd_client.interface,
      address_b,
      &test_cache);
    mock().enable();
  }

  void a_subscription_should_be_requested_for(uint8_t address)
  {
    mock()
      .expectOneCall("subscribe")
      .onObject(&erd_client)
      .withParameter("address", address)
      .andReturnValue(true);
  }

  void after_a_subscription_is_added_or_retained_for(uint8_t address)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_added_or_retained;
    args.address = address;
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void given_both_subscriptions_are_active()
  {
    mock().disable();
    after_a_subscription_is_added_or_retained_for(address_a);
    after_a_subscription_is_added_or_retained_for(address_b);
    mock().enable();
  }

  template <typename T>
  void when_an_erd_publication_is_received(uint8_t publisher_address, tiny_erd_t erd, T data)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    args.type = tiny_gea3_erd_client_activity_type_subscription_publication_received;
    args.address = publisher_address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data = &data;
    args.subscription_publication_received.data_size = sizeof(data);
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
  }

  void after(tiny_timer_ticks_t ticks)
  {
    tiny_timer_group_double_elapse_time(&timer_group, ticks);
  }

  void nothing_should_happen()
  {
  }
};

TEST(erd_bridge_subscribe_dual, each_bridge_subscribes_to_its_own_address_at_init)
{
  a_subscription_should_be_requested_for(address_a);
  a_subscription_should_be_requested_for(address_b);
  erd_bridge_subscribe_init(
    &bridge_a,
    &timer_group.timer_group,
    &erd_client.interface,
    address_a,
    &test_cache);
  erd_bridge_subscribe_init(
    &bridge_b,
    &timer_group.timer_group,
    &erd_client.interface,
    address_b,
    &test_cache);
}


TEST(erd_bridge_subscribe_dual, each_bridge_independently_retains_its_subscription)
{
  given_both_bridges_are_initialized();
  given_both_subscriptions_are_active();

  nothing_should_happen();
  after(subscription_retention_period - 1);

  mock()
    .expectOneCall("retain_subscription")
    .onObject(&erd_client)
    .withParameter("address", address_a)
    .andReturnValue(true);
  mock()
    .expectOneCall("retain_subscription")
    .onObject(&erd_client)
    .withParameter("address", address_b)
    .andReturnValue(true);
  after(1);
}

TEST(erd_bridge_subscribe_dual, resubscribing_one_bridge_does_not_affect_the_other)
{
  given_both_bridges_are_initialized();
  given_both_subscriptions_are_active();

  // bridge_b's host comes back online: only bridge_b should resubscribe
  mock()
    .expectOneCall("subscribe")
    .onObject(&erd_client)
    .withParameter("address", address_b)
    .andReturnValue(true);
  tiny_gea3_erd_client_on_activity_args_t args;
  args.type = tiny_gea3_erd_client_activity_type_subscription_host_came_online;
  args.address = address_b;
  tiny_gea3_erd_client_double_trigger_activity_event(&erd_client, &args);
}
