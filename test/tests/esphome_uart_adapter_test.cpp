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
