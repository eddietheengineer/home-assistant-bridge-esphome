# ESPHome UART Adapter

## Purpose

Adapts an ESPHome `UARTComponent` to the `i_tiny_uart` interface expected by the tiny-gea-api protocol stack. Polls the UART for received bytes at high frequency and publishes them as events.

## Public API

| Function | Description |
|----------|-------------|
| `esphome_uart_adapter_init(self, timer_group, uart)` | Initialize adapter with timer group and ESPHome UART component. Sets `enabled` to true. |
| `esphome_uart_adapter_set_enabled(self, enabled)` | Enable or disable the adapter. When disabled, the poll callback returns early without processing bytes. Used in dual-UART configurations to switch between GEA2 and GEA3. |
## Internal i_tiny_uart API Implementation

The adapter implements the `i_tiny_uart_api_t` interface:

| Callback | Description |
|----------|-------------|
| `send(byte)` | Write byte to UART, set `sent` flag |
| `on_send_complete()` | Return event pointer for send completion notifications |
| `on_receive()` | Return event pointer for receive notifications |

## Dependencies

- `esphome::uart::UARTComponent` — ESPHome UART abstraction
- `i_tiny_uart` — interface from tiny-gea-api
- `tiny_event` — event pub/sub system
- `tiny_timer` — periodic polling timer

## Key Design Decisions

- **Snapshot-based receive**: The available byte count is snapshotted once at the start of `poll()` to avoid reading bytes that arrive *during* event processing (e.g., ACK reflections or concurrent appliance responses). Without this guard, the receive loop can read an ACK-reflection while `packet_ready` is still set, causing the subsequent appliance response STX to be silently dropped.
- **Zero-period timer**: The poll timer is started with period 0 (as fast as possible in the event loop), ensuring minimal latency between byte arrival and event publication.
- **Send completion deferral**: The `sent` flag is set synchronously in `send()` and cleared in the next `poll()` call, publishing the `send_complete_event` then. This matches the half-duplex nature of the GEA bus where send completion is detected by the protocol stack's state machine.

## Testing

Covered by integration tests in `test/tests/` through the full bridge UART communication flow. The snapshot-based receive logic is critical for GEA2 half-duplex operation and is tested implicitly through large-payload ERD reads.
