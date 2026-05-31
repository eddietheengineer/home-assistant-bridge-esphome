/*
 * @file
 * @brief Unit tests for the ApplianceSideStateMachine class.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#ifdef new
#undef new
#endif

#include "appliance_side_state_machine.h"
#include "erd_state_table.h"
#include "global_state_registry.h"
#include "write_queue.h"
#include <cstring>

using namespace esphome::geappliances_bridge;

TEST_GROUP(appliance_side_state_machine)
{
  ErdStateTable state_table_;
  GlobalStateRegistry registry_;
  WriteQueue queue_;

  void setup() {}
  void teardown() {}
};

TEST(appliance_side_state_machine, starts_in_idle_state)
{
  ApplianceSideStateMachine fsm(&state_table_, &registry_, &queue_, nullptr);
  CHECK_EQUAL(static_cast<int>(ApplianceSideState::IDLE),
              static_cast<int>(fsm.get_current_state()));
}

TEST(appliance_side_state_machine, transitions_to_running_on_address)
{
  ApplianceSideStateMachine fsm(&state_table_, &registry_, &queue_, nullptr);
  registry_.set_appliance_address(0xC0);
  fsm.loop();
  CHECK_EQUAL(static_cast<int>(ApplianceSideState::RUNNING),
              static_cast<int>(fsm.get_current_state()));
}

TEST(appliance_side_state_machine, stays_idle_without_address)
{
  ApplianceSideStateMachine fsm(&state_table_, &registry_, &queue_, nullptr);
  fsm.loop();
  CHECK_EQUAL(static_cast<int>(ApplianceSideState::IDLE),
              static_cast<int>(fsm.get_current_state()));
}

TEST(appliance_side_state_machine, set_config_applies_polling)
{
  ApplianceSideStateMachine fsm(&state_table_, &registry_, &queue_, nullptr);
  ApplianceSideConfig config;
  config.enable_subscriptions = false;
  config.enable_polling = true;
  config.polling_interval_ms = 10000;
  config.only_publish_on_change = true;
  config.polling_erds.push_back(0x1001);
  config.polling_erds.push_back(0x1002);
  fsm.set_config(config);

  PollingHandler* ph = fsm.get_polling_handler();
  CHECK(ph != nullptr);
  CHECK_EQUAL(2, ph->get_polling_erds().size());
  CHECK_EQUAL(10000, ph->get_polling_interval());
}

TEST(appliance_side_state_machine, get_handlers_return_non_null)
{
  ApplianceSideStateMachine fsm(&state_table_, &registry_, &queue_, nullptr);
  CHECK(fsm.get_subscription_handler() != nullptr);
  CHECK(fsm.get_polling_handler() != nullptr);
  CHECK(fsm.get_write_handler() != nullptr);
}
