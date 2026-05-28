// =============================================================================
// MODULE GOAL
// =============================================================================
// Goal: Adapt ESPHome's UART API to the i_tiny_uart_t interface expected by
//       the GEA protocol stack.
//
// Responsibilities:
//   - Poll the ESPHome UART component each loop() tick and emit receive events
//   - Report send_complete once a byte has been transmitted
//   - Support enable/disable for dual-UART configurations
//
// NOT responsible for:
//   - Protocol framing or message timing
//   - Routing bytes between GEA3 and GEA2 UARTs (caller controls enable flag)
//
// Dependencies:
//   - esphome::uart::UARTComponent
//   - i_tiny_uart.h, tiny_event.h, tiny_timer.h
// =============================================================================

#pragma once

#include "esphome/components/uart/uart.h"

extern "C" {
#include "hal/i_tiny_uart.h"
#include "tiny_event.h"
#include "tiny_timer.h"
}

typedef struct {
  i_tiny_uart_t interface;
  tiny_timer_group_t* timer_group;
  esphome::uart::UARTComponent* uart;
  tiny_event_t send_complete_event;
  tiny_event_t receive_event;
  tiny_timer_t timer;
  bool sent;
  bool enabled;
} esphome_uart_adapter_t;

#ifdef __cplusplus
extern "C" {
#endif

void esphome_uart_adapter_init(
  esphome_uart_adapter_t* self,
  tiny_timer_group_t* timer_group,
  esphome::uart::UARTComponent* uart);

void esphome_uart_adapter_set_enabled(
  esphome_uart_adapter_t* self,
  bool enabled);

#ifdef __cplusplus
}
#endif
