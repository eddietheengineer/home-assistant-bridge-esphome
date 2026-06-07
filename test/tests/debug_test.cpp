#include "double/tiny_gea3_erd_client_double.hpp"
#include "double/tiny_timer_group_double.hpp"
#include "subscription_handler.h"
#include "erd_state_table.h"
#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <cstring>
#include <cstdio>

using namespace esphome::geappliances_bridge;

static SubscriptionHandler* g_last_handler = nullptr;

TEST_GROUP(subscription_handler_debug)
{
  ErdStateTable state_table_;
  tiny_gea3_erd_client_double_t erd_client_double_;
  tiny_timer_group_double_t timer_group_;

  void setup()
  {
    mock().clear();
    tiny_gea3_erd_client_double_init(&erd_client_double_);
    tiny_timer_group_double_init(&timer_group_);
    if (g_last_handler != nullptr) {
      g_last_handler->stop();
    }
    g_last_handler = nullptr;
  }

  SubscriptionHandler* create_and_start(uint8_t address)
  {
    SubscriptionHandler* handler = new SubscriptionHandler(
      &erd_client_double_.interface, &state_table_, &timer_group_.timer_group);
    handler->start(address);
    g_last_handler = handler;
    return handler;
  }

  void trigger_activity(tiny_gea3_erd_client_activity_type_t type,
                        uint8_t address,
                        tiny_erd_t erd = 0,
                        const uint8_t* data = nullptr,
                        uint8_t data_size = 0)
  {
    tiny_gea3_erd_client_on_activity_args_t args;
    memset(&args, 0, sizeof(args));
    args.type = type;
    args.address = address;
    args.subscription_publication_received.erd = erd;
    args.subscription_publication_received.data =
      reinterpret_cast<const void*>(data);
    args.subscription_publication_received.data_size = data_size;
    fprintf(stderr, "DEBUG: About to trigger activity event\n");
    tiny_gea3_erd_client_double_trigger_activity_event(&erd_client_double_, &args);
    fprintf(stderr, "DEBUG: Trigger activity event returned\n");
  }
};

TEST(subscription_handler_debug, subscription_publication_writes_to_state_table)
{
  mock().expectOneCall("subscribe")
    .withParameter("address", 0xC0)
    .andReturnValue(true);

  fprintf(stderr, "DEBUG: Before create_and_start\n");
  SubscriptionHandler* handler = create_and_start(0xC0);
  fprintf(stderr, "DEBUG: After create_and_start\n");

  uint8_t value[] = {0x01, 0x02};
  fprintf(stderr, "DEBUG: Before trigger_activity\n");
  trigger_activity(tiny_gea3_erd_client_activity_type_subscription_publication_received,
                   0xC0, 0x1001, value, 2);
  fprintf(stderr, "DEBUG: After trigger_activity\n");

  uint8_t size_out;
  const uint8_t* result = state_table_.get_erd_value(0x1001, size_out);
  CHECK(result != nullptr);
  CHECK_EQUAL(2, size_out);
  CHECK_EQUAL(0x01, result[0]);
  CHECK_EQUAL(0x02, result[1]);

  delete handler;
  g_last_handler = nullptr;
}
