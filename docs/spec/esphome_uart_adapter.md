# ESPHome UART Adapter — Specification

## 1. Overview

### 1.1 Purpose

Adapts ESPHome's `UARTComponent` to the `i_tiny_uart_t` interface expected by the tiny-gea-api protocol stack. Polls the UART for received bytes at high frequency and publishes them as events.

### 1.2 Responsibilities

- Poll the ESPHome UART component each `loop()` tick and emit receive events
- Report `send_complete` once a byte has been transmitted
- Support enable/disable for dual-UART configurations

### 1.3 Not Responsible For

- Protocol framing or message timing
- Routing bytes between GEA3 and GEA2 UARTs (caller controls enable flag)

---

## 2. Data Structures

```c
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
```

| Field | Description |
|-------|-------------|
| `interface` | `i_tiny_uart_t` struct containing an `.api` member pointer to the static `api` vtable |
| `timer_group` | Shared timer group for the poll timer |
| `uart` | Pointer to the ESPHome `UARTComponent` |
| `send_complete_event` | Event published when a byte has been transmitted |
| `receive_event` | Event published for each received byte |
| `timer` | Periodic poll timer (period 0 — as fast as possible) |
| `sent` | Flag set by `send()`, cleared by `poll()` |
| `enabled` | Gate — when `false`, `poll()` returns immediately |

---

## 3. Public API

### 3.1 Initialization

```c
void esphome_uart_adapter_init(
  esphome_uart_adapter_t* self,
  tiny_timer_group_t* timer_group,
  esphome::uart::UARTComponent* uart);
```

| Parameter | Description |
|-----------|-------------|
| `self` | Pointer to the adapter struct |
| `timer_group` | Shared timer group for the poll timer |
| `uart` | ESPHome UART component to wrap |

Sets `interface.api` to the static `api` vtable, initializes both events, and arms a periodic poll timer with period 0.

### 3.2 Enable/Disable

```c
void esphome_uart_adapter_set_enabled(
  esphome_uart_adapter_t* self,
  bool enabled);
```

Sets or clears the `enabled` flag. When `false`, the poll callback returns immediately without reading bytes or publishing events. Used in dual-UART configurations to prevent the inactive adapter from filling event queues.

---

## 4. i_tiny_uart API Implementation

The adapter implements the `i_tiny_uart_api_t` vtable:

### 4.1 `send(byte)`

Sets `sent = true` first, then writes the byte to the UART via `uart->write_byte(byte)`. The `send_complete_event` is not published here — it is deferred to the next `poll()` call.

### 4.2 `on_send_complete()`

Returns a pointer to `send_complete_event.interface`. Callers subscribe to this event to know when the previously sent byte has been acknowledged by the protocol stack.

### 4.3 `on_receive()`

Returns a pointer to `receive_event.interface`. Callers subscribe to this event to receive byte-by-byte data from the UART.

---

## 5. Poll Loop

The `poll()` function is the timer callback, invoked as fast as the event loop allows (period 0).

### 5.1 Enable Gate

If `self->enabled` is `false`, `poll()` returns immediately. This is critical in dual-UART configurations where both adapters share the same `timer_group` — only the active adapter should process bytes.

### 5.2 Snapshot-Based Receive

The available byte count is snapshotted **once** at the start of `poll()`:

```c
int rx_bytes = self->uart->available();
```

Then each byte is read and published as a `receive_event`:

```c
while (rx_bytes--) {
    uint8_t byte;
    self->uart->read_byte(&byte);
    tiny_uart_on_receive_args_t args = { byte };
    tiny_event_publish(&self->receive_event, &args);
}
```

**This snapshot is critical.** Without it, bytes that arrive *during* event processing (e.g., the half-duplex reflection of a byte just sent by `send_next_byte()`, or an appliance response that begins arriving while `send_ack()` is executing) would be consumed in the same poll cycle. This causes the subsequent appliance response STX to be silently dropped in `state_idle_cooldown` because the `!packet_ready` guard fails. The manifestation is ERD reads for large payloads (e.g., 32-byte model number) always failing while small payloads (e.g., 1-byte appliance type) succeed by chance.

### 5.3 Send Completion Deferral

After processing receive bytes, if `sent` is `true`, it is cleared and the `send_complete_event` is published:

```c
if (self->sent) {
    self->sent = false;
    tiny_event_publish(&self->send_complete_event, nullptr);
}
```

This deferral matches the half-duplex nature of the GEA bus where send completion is detected by the protocol stack's state machine.

---

## 6. Invariants

1. **Snapshot-based receive:** The byte count is snapshotted once per poll. Bytes arriving during event processing are deferred to the next poll.
2. **Zero-period timer:** The poll timer is started with period 0, ensuring minimal latency between byte arrival and event publication.
3. **Send completion deferral:** `sent` is set synchronously in `send()` and cleared in the next `poll()` call.
4. **Enable gate:** When `enabled` is `false`, `poll()` returns immediately — no bytes are read, no events are published.
5. **No heap allocation:** All state is embedded in the struct. The vtable is a file-scope static.
6. **Event-driven:** Both receive and send completion are communicated via `tiny_event` pub/sub, not direct callbacks.

---

## 7. Dependencies

| Dependency | Role |
|------------|------|
| `esphome::uart::UARTComponent` | ESPHome UART abstraction (read/write bytes) |
| `esphome/components/uart/uart.h` | ESPHome UART component header |
| `hal/i_tiny_uart.h` | Interface from tiny-gea-api |
| `tiny_event.h` | Event pub/sub system |
| `tiny_timer.h` | Periodic polling timer |

---

## 8. Known Limitations

1. **Polling-based, not interrupt-driven:** The adapter polls the UART at the event loop frequency rather than using a hardware interrupt. This is acceptable because the GEA bus operates at low baud rates (19200–230400) and the ESPHome event loop runs frequently enough.
2. **Snapshot guard is critical:** The snapshot-based receive is not just an optimization — it is required for correct half-duplex operation. Removing or weakening the snapshot causes large-payload ERD reads to fail.
3. **No flow control:** The adapter does not implement hardware or software flow control. The GEA protocol handles this at a higher level.
4. **Single UART per adapter:** Each adapter instance wraps exactly one `UARTComponent`. Dual-UART configurations use two adapter instances with `set_enabled()` to gate the inactive one.
