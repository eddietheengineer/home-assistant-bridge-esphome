/*!
 * @file
 * @brief Unit tests for the ESPHome UART adapter.
 *
 * Validates initialization, interface setup, and basic struct state.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "esphome_uart_adapter.h"
#include "double/esphome_hal_double.hpp"
#include "double/tiny_timer_group_double.hpp"
#include "esphome/components/uart/uart.h"

#include <cstring>

/* ------------------------------------------------------------------ */
/* Mock UARTComponent for testing                                      */
/* ------------------------------------------------------------------ */

struct MockUartComponent : public esphome::uart::UARTComponent {
  int available_count;
  int read_count;
  int write_count;
  int write_byte_count;
  uint8_t last_written_byte;
  uint8_t read_buffer[256];
  int read_buffer_index;

  MockUartComponent()
    : available_count(0), read_count(0), write_count(0),
      write_byte_count(0), last_written_byte(0),
      read_buffer_index(0)
  {
    std::memset(read_buffer, 0, sizeof(read_buffer));
  }

  int available() override { return available_count; }

  int read() override
  {
    if (read_buffer_index < (int)sizeof(read_buffer)) {
      return read_buffer[read_buffer_index++];
    }
    read_count++;
    return -1;
  }

  void write(uint8_t data) override
  {
    last_written_byte = data;
    write_count++;
  }

  void write(const uint8_t* data, size_t len) override
  {
    for (size_t i = 0; i < len; i++) {
      last_written_byte = data[i];
      write_count++;
    }
  }

  void read_byte(uint8_t* byte) override
  {
    if (read_buffer_index < (int)sizeof(read_buffer)) {
      *byte = read_buffer[read_buffer_index++];
    } else {
      *byte = 0;
      read_count++;
    }
  }

  void write_byte(uint8_t byte) override
  {
    last_written_byte = byte;
    write_byte_count++;
  }

  void clear()
  {
    available_count = 0;
    read_count = 0;
    write_count = 0;
    write_byte_count = 0;
    last_written_byte = 0;
    read_buffer_index = 0;
    std::memset(read_buffer, 0, sizeof(read_buffer));
  }
};

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(esphome_uart_adapter)
{
  esphome_uart_adapter_t adapter;
  tiny_timer_group_double_t timer_group;
  MockUartComponent mock_uart;

  void setup()
  {
    mock().strictOrder();
    tiny_timer_group_double_init(&timer_group);
    mock_uart.clear();
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    mock().clear();
  }

  void init_adapter()
  {
    esphome_uart_adapter_init(&adapter, &timer_group.timer_group, &mock_uart);
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(esphome_uart_adapter, init_stores_uart_pointer)
{
  init_adapter();
  CHECK(adapter.uart == &mock_uart);
}

TEST(esphome_uart_adapter, init_stores_timer_group)
{
  init_adapter();
  CHECK(adapter.timer_group == &timer_group.timer_group);
}

TEST(esphome_uart_adapter, init_sets_interface_api)
{
  init_adapter();
  CHECK(adapter.interface.api != nullptr);
}

TEST(esphome_uart_adapter, init_sets_sent_to_false)
{
  init_adapter();
  CHECK_FALSE(adapter.sent);
}

TEST(esphome_uart_adapter, init_sets_up_send_complete_event)
{
  init_adapter();
  CHECK(adapter.send_complete_event.interface.api != nullptr);
}

TEST(esphome_uart_adapter, init_sets_up_receive_event)
{
  init_adapter();
  CHECK(adapter.receive_event.interface.api != nullptr);
}

/* ------------------------------------------------------------------ */
/* interface function pointer tests                                     */
/* ------------------------------------------------------------------ */

TEST(esphome_uart_adapter, init_api_send_is_not_null)
{
  init_adapter();
  CHECK(adapter.interface.api->send != nullptr);
}

TEST(esphome_uart_adapter, init_api_on_send_complete_is_not_null)
{
  init_adapter();
  CHECK(adapter.interface.api->on_send_complete != nullptr);
}

TEST(esphome_uart_adapter, init_api_on_receive_is_not_null)
{
  init_adapter();
  CHECK(adapter.interface.api->on_receive != nullptr);
}

/* ------------------------------------------------------------------ */
/* on_send_complete / on_receive return correct event pointers          */
/* ------------------------------------------------------------------ */

TEST(esphome_uart_adapter, on_send_complete_returns_send_complete_event_interface)
{
  init_adapter();
  i_tiny_event_t* event = adapter.interface.api->on_send_complete(&adapter.interface);
  CHECK(event == &adapter.send_complete_event.interface);
}

TEST(esphome_uart_adapter, on_receive_returns_receive_event_interface)
{
  init_adapter();
  i_tiny_event_t* event = adapter.interface.api->on_receive(&adapter.interface);
  CHECK(event == &adapter.receive_event.interface);
}

/* ------------------------------------------------------------------ */
/* enabled flag tests                                                   */
/* ------------------------------------------------------------------ */

TEST(esphome_uart_adapter, init_sets_enabled_to_true)
{
  init_adapter();
  CHECK(adapter.enabled);
}

TEST(esphome_uart_adapter, set_enabled_can_disable_adapter)
{
  init_adapter();
  esphome_uart_adapter_set_enabled(&adapter, false);
  CHECK_FALSE(adapter.enabled);
}

TEST(esphome_uart_adapter, set_enabled_can_re_enable_adapter)
{
  init_adapter();
  esphome_uart_adapter_set_enabled(&adapter, false);
  esphome_uart_adapter_set_enabled(&adapter, true);
  CHECK(adapter.enabled);
}

/* ------------------------------------------------------------------ */
/* poll() respects enabled flag                                         */
/* ------------------------------------------------------------------ */

static void receive_event_callback(void* context, const void* args)
{
  (void)context;
  uint8_t byte = reinterpret_cast<const tiny_uart_on_receive_args_t*>(args)->byte;
  mock().actualCall("receive_event_published").withParameter("byte", byte);
}

static void send_complete_event_callback(void* context, const void* args)
{
  (void)context; (void)args;
  mock().actualCall("send_complete_event_published");
}

TEST(esphome_uart_adapter, poll_does_not_publish_receive_events_when_disabled)
{
  init_adapter();
  esphome_uart_adapter_set_enabled(&adapter, false);

  // Pretend there are bytes available on the UART
  mock_uart.read_buffer[0] = 0xAA;
  mock_uart.read_buffer[1] = 0xBB;
  mock_uart.available_count = 2;

  // Trigger the poll callback by running the timer group once.
  // Using elapse_time() with a period-0 timer causes an infinite loop
  // because the timer re-schedules immediately, so we call run() directly.
  tiny_timer_group_run(&timer_group.timer_group);

  // No receive events should have been published
  // (no mock expectations = no calls should have been made)
}

TEST(esphome_uart_adapter, poll_publishes_receive_events_when_enabled)
{
  init_adapter();

  // Subscribe to the receive event so we can verify it's called
  tiny_event_subscription_t sub;
  tiny_event_subscription_init(&sub, nullptr, receive_event_callback);
  tiny_event_subscribe(&adapter.receive_event.interface, &sub);

  // Pretend there are bytes available on the UART
  mock_uart.read_buffer[0] = 0xAA;
  mock_uart.read_buffer[1] = 0xBB;
  mock_uart.available_count = 2;

  // Expect both bytes to be published
  mock().expectOneCall("receive_event_published").withParameter("byte", 0xAA);
  mock().expectOneCall("receive_event_published").withParameter("byte", 0xBB);

  // Trigger the poll callback by running the timer group once
  tiny_timer_group_run(&timer_group.timer_group);

  // Unsubscribe to clean up
  tiny_event_unsubscribe(&adapter.receive_event.interface, &sub);
}

TEST(esphome_uart_adapter, poll_does_not_publish_send_complete_when_disabled)
{
  init_adapter();
  esphome_uart_adapter_set_enabled(&adapter, false);

  // Mark that a byte was sent
  adapter.sent = true;

  // Trigger the poll callback by running the timer group once
  tiny_timer_group_run(&timer_group.timer_group);

  // No send_complete event should have been published
  // Verify sent flag was NOT cleared (poll returned early)
  CHECK(adapter.sent);
}

TEST(esphome_uart_adapter, poll_publishes_send_complete_when_enabled)
{
  init_adapter();

  // Subscribe to the send_complete event
  tiny_event_subscription_t sub;
  tiny_event_subscription_init(&sub, nullptr, send_complete_event_callback);
  tiny_event_subscribe(&adapter.send_complete_event.interface, &sub);

  // Mark that a byte was sent
  adapter.sent = true;

  // Set available to 0 so poll only processes the send_complete
  mock_uart.available_count = 0;

  mock().expectOneCall("send_complete_event_published");

  // Trigger the poll callback by running the timer group once
  tiny_timer_group_run(&timer_group.timer_group);

  // Verify sent flag was cleared
  CHECK_FALSE(adapter.sent);

  // Unsubscribe to clean up
  tiny_event_unsubscribe(&adapter.send_complete_event.interface, &sub);
}

TEST(esphome_uart_adapter, poll_skips_receive_but_still_checks_sent_when_disabled)
{
  // When disabled, the entire poll() returns early, so sent is NOT cleared.
  // This is intentional - the inactive adapter should not process anything.
  init_adapter();
  esphome_uart_adapter_set_enabled(&adapter, false);

  mock_uart.read_buffer[0] = 0xAA;
  mock_uart.available_count = 1;
  adapter.sent = true;

  tiny_timer_group_run(&timer_group.timer_group);

  // Both receive processing AND sent clearing should be skipped
  CHECK(adapter.sent);  // sent flag unchanged
  CHECK(mock_uart.read_buffer_index == 0);  // no bytes read
}

/* ------------------------------------------------------------------ */
/* dual-adapter shared timer group tests                                */
/* ------------------------------------------------------------------ */

static int adapter_a_poll_count = 0;
static int adapter_b_poll_count = 0;

static void adapter_a_poll_callback(void* context, const void* args)
{
  (void)context; (void)args;
  adapter_a_poll_count++;
}

static void adapter_b_poll_callback(void* context, const void* args)
{
  (void)context; (void)args;
  adapter_b_poll_count++;
}

TEST(esphome_uart_adapter, two_adapters_in_same_timer_group_both_fire_when_run_twice)
{
  // Simulates the scenario where both GEA3 and GEA2 UART adapters register
  // period-0 poll timers in the same timer group.  tiny_timer_group_run()
  // only services one timer per call, so calling it once only fires one
  // adapter's poll.  Calling it twice fires both.
  esphome_uart_adapter_t adapter_a;
  esphome_uart_adapter_t adapter_b;
  tiny_timer_group_double_t tg;

  adapter_a_poll_count = 0;
  adapter_b_poll_count = 0;

  mock().disable();
  tiny_timer_group_double_init(&tg);
  mock_uart.clear();
  esphome_uart_adapter_init(&adapter_a, &tg.timer_group, &mock_uart);
  esphome_uart_adapter_init(&adapter_b, &tg.timer_group, &mock_uart);

  // Subscribe to both receive events so we can count how many times each
  // adapter's poll callback actually processes bytes.
  tiny_event_subscription_t sub_a;
  tiny_event_subscription_t sub_b;
  tiny_event_subscription_init(&sub_a, nullptr, adapter_a_poll_callback);
  tiny_event_subscription_init(&sub_b, nullptr, adapter_b_poll_callback);
  tiny_event_subscribe(&adapter_a.receive_event.interface, &sub_a);
  tiny_event_subscribe(&adapter_b.receive_event.interface, &sub_b);

  // Put a byte on the UART so both adapters have something to process.
  mock_uart.read_buffer[0] = 0xAA;
  mock_uart.available_count = 1;

  // Call tiny_timer_group_run() twice — once for each adapter's poll timer.
  // This mirrors what run_protocol_stack_() does when both UARTs are configured.
  tiny_timer_group_run(&tg.timer_group);
  tiny_timer_group_run(&tg.timer_group);

  // Both adapters should have processed the byte.
  CHECK(adapter_a_poll_count == 1);
  CHECK(adapter_b_poll_count == 1);

  tiny_event_unsubscribe(&adapter_a.receive_event.interface, &sub_a);
  tiny_event_unsubscribe(&adapter_b.receive_event.interface, &sub_b);
  mock().enable();
}

TEST(esphome_uart_adapter, disabled_adapter_does_not_process_bytes_even_when_timer_fires)
{
  // Verifies that when an adapter is disabled, its poll() returns early
  // even though the timer fires — so the other adapter's poll is the only
  // one that actually processes UART bytes.
  esphome_uart_adapter_t adapter_active;
  esphome_uart_adapter_t adapter_inactive;
  tiny_timer_group_double_t tg;

  adapter_a_poll_count = 0;
  adapter_b_poll_count = 0;

  mock().disable();
  tiny_timer_group_double_init(&tg);
  mock_uart.clear();
  esphome_uart_adapter_init(&adapter_active, &tg.timer_group, &mock_uart);
  esphome_uart_adapter_init(&adapter_inactive, &tg.timer_group, &mock_uart);
  esphome_uart_adapter_set_enabled(&adapter_inactive, false);

  tiny_event_subscription_t sub_active;
  tiny_event_subscription_t sub_inactive;
  tiny_event_subscription_init(&sub_active, nullptr, adapter_a_poll_callback);
  tiny_event_subscription_init(&sub_inactive, nullptr, adapter_b_poll_callback);
  tiny_event_subscribe(&adapter_active.receive_event.interface, &sub_active);
  tiny_event_subscribe(&adapter_inactive.receive_event.interface, &sub_inactive);

  mock_uart.read_buffer[0] = 0xAA;
  mock_uart.available_count = 1;

  // Drain both timers.
  tiny_timer_group_run(&tg.timer_group);
  tiny_timer_group_run(&tg.timer_group);

  // Only the active adapter should have processed the byte.
  CHECK(adapter_a_poll_count == 1);
  CHECK(adapter_b_poll_count == 0);

  tiny_event_unsubscribe(&adapter_active.receive_event.interface, &sub_active);
  tiny_event_unsubscribe(&adapter_inactive.receive_event.interface, &sub_inactive);
  mock().enable();
}

TEST(esphome_uart_adapter, single_timer_group_run_only_fires_one_of_two_period_0_timers)
{
  // Demonstrates the bug: calling tiny_timer_group_run() once with two
  // period-0 timers only fires one of them.  This is why run_protocol_stack_()
  // must call it twice when both UARTs are configured.
  esphome_uart_adapter_t adapter_a;
  esphome_uart_adapter_t adapter_b;
  tiny_timer_group_double_t tg;

  adapter_a_poll_count = 0;
  adapter_b_poll_count = 0;

  mock().disable();
  tiny_timer_group_double_init(&tg);
  mock_uart.clear();
  esphome_uart_adapter_init(&adapter_a, &tg.timer_group, &mock_uart);
  esphome_uart_adapter_init(&adapter_b, &tg.timer_group, &mock_uart);

  tiny_event_subscription_t sub_a;
  tiny_event_subscription_t sub_b;
  tiny_event_subscription_init(&sub_a, nullptr, adapter_a_poll_callback);
  tiny_event_subscription_init(&sub_b, nullptr, adapter_b_poll_callback);
  tiny_event_subscribe(&adapter_a.receive_event.interface, &sub_a);
  tiny_event_subscribe(&adapter_b.receive_event.interface, &sub_b);

  mock_uart.read_buffer[0] = 0xAA;
  mock_uart.available_count = 1;

  // Call tiny_timer_group_run() only ONCE — only one adapter fires.
  tiny_timer_group_run(&tg.timer_group);

  // Exactly one adapter processed the byte (whichever timer was first in the list).
  CHECK(adapter_a_poll_count + adapter_b_poll_count == 1);

  tiny_event_unsubscribe(&adapter_a.receive_event.interface, &sub_a);
  tiny_event_unsubscribe(&adapter_b.receive_event.interface, &sub_b);
  mock().enable();
}
