/*
 * @file
 * @brief Unit tests for the GlobalStateRegistry class.
 *
 * Validates read/write accessors, event subscriptions, write-once semantics
 * for device_id, and no-fire-on-same-value behavior.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "global_state_registry.h"
#include <string>

using namespace esphome::geappliances_bridge;

/* ------------------------------------------------------------------ */
/* Event callback counters                                              */
/* ------------------------------------------------------------------ */

static int device_id_ready_count = 0;
static int appliance_address_changed_count = 0;
static int gea_protocol_type_changed_count = 0;
static int bridge_state_changed_count = 0;

static void device_id_ready_callback(void*, const void*)
{
  device_id_ready_count++;
}

static void appliance_address_changed_callback(void*, const void*)
{
  appliance_address_changed_count++;
}

static void gea_protocol_type_changed_callback(void*, const void*)
{
  gea_protocol_type_changed_count++;
}

static void bridge_state_changed_callback(void*, const void*)
{
  bridge_state_changed_count++;
}

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(global_state_registry)
{
  GlobalStateRegistry registry;
  tiny_event_subscription_t sub_device_id_;
  tiny_event_subscription_t sub_appliance_address_;
  tiny_event_subscription_t sub_gea_protocol_type_;
  tiny_event_subscription_t sub_bridge_state_;

  void setup()
  {
    device_id_ready_count = 0;
    appliance_address_changed_count = 0;
    gea_protocol_type_changed_count = 0;
    bridge_state_changed_count = 0;
  }

  void subscribe_all()
  {
    tiny_event_subscription_init(&sub_device_id_, nullptr, device_id_ready_callback);
    tiny_event_subscribe(registry.on_device_id_ready(), &sub_device_id_);

    tiny_event_subscription_init(&sub_appliance_address_, nullptr, appliance_address_changed_callback);
    tiny_event_subscribe(registry.on_appliance_address_changed(), &sub_appliance_address_);

    tiny_event_subscription_init(&sub_gea_protocol_type_, nullptr, gea_protocol_type_changed_callback);
    tiny_event_subscribe(registry.on_gea_protocol_type_changed(), &sub_gea_protocol_type_);

    tiny_event_subscription_init(&sub_bridge_state_, nullptr, bridge_state_changed_callback);
    tiny_event_subscribe(registry.on_bridge_state_changed(), &sub_bridge_state_);
  }

  void teardown()
  {
    tiny_event_unsubscribe(registry.on_device_id_ready(), &sub_device_id_);
    tiny_event_unsubscribe(registry.on_appliance_address_changed(), &sub_appliance_address_);
    tiny_event_unsubscribe(registry.on_gea_protocol_type_changed(), &sub_gea_protocol_type_);
    tiny_event_unsubscribe(registry.on_bridge_state_changed(), &sub_bridge_state_);
  }
};

/* ------------------------------------------------------------------ */
/* Default state                                                        */
/* ------------------------------------------------------------------ */

TEST(global_state_registry, default_state_is_correct)
{
  CHECK_EQUAL("", registry.get_device_id());
  CHECK_EQUAL(0, registry.get_appliance_address());
  CHECK_EQUAL(0, registry.get_gea_protocol_type());
  CHECK_EQUAL(static_cast<int>(BridgeState::STARTING), static_cast<int>(registry.get_bridge_state()));
}

/* ------------------------------------------------------------------ */
/* device_id — write-once with event                                   */
/* ------------------------------------------------------------------ */

TEST(global_state_registry, set_device_id_fires_event)
{
  subscribe_all();

  registry.set_device_id("SplitAC_ModelX_SN123");

  CHECK_EQUAL("SplitAC_ModelX_SN123", registry.get_device_id());
  CHECK_EQUAL(1, device_id_ready_count);
}

TEST(global_state_registry, set_device_id_is_write_once)
{
  subscribe_all();

  registry.set_device_id("first");
  registry.set_device_id("second");

  CHECK_EQUAL("first", registry.get_device_id());
  CHECK_EQUAL(1, device_id_ready_count);  // only fired once
}

TEST(global_state_registry, set_device_id_no_fire_when_already_set)
{
  subscribe_all();

  registry.set_device_id("only-one");
  registry.set_device_id("ignored");

  CHECK_EQUAL(1, device_id_ready_count);
}

/* ------------------------------------------------------------------ */
/* appliance_address                                                    */
/* ------------------------------------------------------------------ */

TEST(global_state_registry, set_appliance_address_fires_event)
{
  subscribe_all();

  registry.set_appliance_address(0xC0);

  CHECK_EQUAL(0xC0, registry.get_appliance_address());
  CHECK_EQUAL(1, appliance_address_changed_count);
}

TEST(global_state_registry, set_appliance_address_no_fire_when_same)
{
  subscribe_all();

  registry.set_appliance_address(0xC0);
  registry.set_appliance_address(0xC0);

  CHECK_EQUAL(1, appliance_address_changed_count);
}

TEST(global_state_registry, set_appliance_address_fires_on_change)
{
  subscribe_all();

  registry.set_appliance_address(0xC0);
  registry.set_appliance_address(0xC1);

  CHECK_EQUAL(0xC1, registry.get_appliance_address());
  CHECK_EQUAL(2, appliance_address_changed_count);
}

/* ------------------------------------------------------------------ */
/* gea_protocol_type                                                    */
/* ------------------------------------------------------------------ */

TEST(global_state_registry, set_gea_protocol_type_fires_event)
{
  subscribe_all();

  registry.set_gea_protocol_type(3);

  CHECK_EQUAL(3, registry.get_gea_protocol_type());
  CHECK_EQUAL(1, gea_protocol_type_changed_count);
}

TEST(global_state_registry, set_gea_protocol_type_no_fire_when_same)
{
  subscribe_all();

  registry.set_gea_protocol_type(2);
  registry.set_gea_protocol_type(2);

  CHECK_EQUAL(1, gea_protocol_type_changed_count);
}

/* ------------------------------------------------------------------ */
/* bridge_state                                                         */
/* ------------------------------------------------------------------ */

TEST(global_state_registry, set_bridge_state_fires_event)
{
  subscribe_all();

  registry.set_bridge_state(BridgeState::RUNNING);

  CHECK_EQUAL(static_cast<int>(BridgeState::RUNNING), static_cast<int>(registry.get_bridge_state()));
  CHECK_EQUAL(1, bridge_state_changed_count);
}

TEST(global_state_registry, set_bridge_state_no_fire_when_same)
{
  subscribe_all();

  /* Default is STARTING, so first set is a no-op */
  registry.set_bridge_state(BridgeState::STARTING);
  CHECK_EQUAL(0, bridge_state_changed_count);

  registry.set_bridge_state(BridgeState::ERROR);
  CHECK_EQUAL(1, bridge_state_changed_count);

  registry.set_bridge_state(BridgeState::ERROR);
  CHECK_EQUAL(1, bridge_state_changed_count);
}

/* ------------------------------------------------------------------ */
/* Event accessors return non-null                                      */
/* ------------------------------------------------------------------ */

TEST(global_state_registry, event_accessors_return_non_null)
{
  CHECK(registry.on_device_id_ready() != nullptr);
  CHECK(registry.on_appliance_address_changed() != nullptr);
  CHECK(registry.on_gea_protocol_type_changed() != nullptr);
  CHECK(registry.on_bridge_state_changed() != nullptr);
}
