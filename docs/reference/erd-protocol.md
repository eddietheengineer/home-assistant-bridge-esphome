# ERD Protocol Reference

Overview of the GE Appliance (GEA) serial protocols and ERD data model.

## ERD (Entity-Relationship Data)

An ERD is a named data point on a GE appliance, identified by a 16-bit hex ID. Each ERD has:

| Property | Description |
|---|---|
| **ERD ID** | 16-bit identifier (e.g., `0x0001`, `0x7001`) |
| **Size** | 1–255 bytes (varies by ERD) |
| **Operations** | `read`, `write`, `publish` (varies by ERD) |
| **Data type** | Raw bytes; interpretation depends on the ERD definition |

### ERD Address Ranges

| Range | Category |
|---|---|
| `0x0000`-`0x00FF` | Common (all appliances) |
| `0x1000`-`0x1FFF` | Refrigeration |
| `0x2000`-`0x2FFF` | Laundry |
| `0x3000`-`0x3FFF` | Dishwasher |
| `0x4000`-`0x4FFF` | Water Heater |
| `0x5000`-`0x5FFF` | Range |
| `0x6000`-`0x6FFF` | Energy |
| `0x7000`-`0x7FFF` | Air Conditioning |
| `0x8000`-`0x8FFF` | Water Filter |
| `0x9000`-`0x9FFF` | Small Appliance |

### Well-Known ERDs

| ERD ID | Name | Size | Operations |
|---|---|---|---|
| `0x0001` | Model Number | 16 bytes | read |
| `0x0002` | Serial Number | 15 bytes | read |
| `0x0008` | Appliance Type | 1 byte | read |
| `0x0092`–`0x010D` | Feature Bit ERDs | varies | read |

## GEA3 Protocol

GEA3 is the primary serial protocol for modern GE appliances.

| Property | Value |
|---|---|
| **Baud rate** | 230400 |
| **Data format** | 8N1 (8 data bits, no parity, 1 stop bit) |
| **Voltage** | 3.3V TTL |
| **Connection** | RJ45 jack |
| **Addressing** | Each board has a unique address (0x00–0xFF); 0xFF is broadcast |
| **Subscription** | Supported — appliance pushes ERD changes |

### GEA3 Message Flow

1. **Read request:** Bridge sends a read command to the appliance's address with the ERD ID.
2. **Response:** Appliance sends the ERD data back with a status code.
3. **Write request:** Bridge sends a write command with ERD ID and data.
4. **Write response:** Appliance acknowledges success or failure.
5. **Subscribe:** Bridge sends a subscription request; appliance publishes ERD changes.

## GEA2 Protocol

GEA2 is the legacy protocol for older appliances.

| Property | Value |
|---|---|
| **Baud rate** | 19200 |
| **Data format** | 8N1 |
| **Voltage** | 3.3V TTL |
| **Connection** | Custom serial connector |
| **Subscription** | Not supported — polling only |

### GEA2 UART Configuration

GEA2 requires special UART settings for reliable communication:

```yaml
uart:
  - id: gea2_uart
    tx_pin: GPIO9
    rx_pin: GPIO10
    baud_rate: 19200
    rx_full_threshold: 1   # deliver each byte immediately
    rx_timeout: 1          # minimise idle-flush latency
```

**Why these settings matter:** GEA2's inter-byte timeout is 6 ms. The default ESP32 UART `rx_full_threshold` batches bytes (~10 ms gap), which exceeds the 6 ms timeout and causes the receive FSM to abort mid-packet. Setting `rx_full_threshold: 1` delivers each byte within ~0.1 ms, keeping the gap well below the limit.

## Protocol Abstraction

The bridge component interacts with both protocols through a unified interface (`i_tiny_gea3_erd_client_t`). When GEA2 is the active protocol, the `gea2_erd_client_adapter` wraps the GEA2 client and presents it as a GEA3 client. `subscribe()` and `retain_subscription()` are no-ops for GEA2.

## Submodule Boundary

The GEA protocol stack implementation lives in the `lib/tiny` and `lib/tiny-gea-api` submodules. The bridge component interacts with them through well-defined C interfaces:

| Interface | Purpose |
|---|---|
| `i_tiny_uart_t` | UART HAL abstraction |
| `i_tiny_gea3_erd_client_t` | GEA3 ERD operations |
| `i_tiny_gea2_erd_client_t` | GEA2 ERD operations |
| `i_tiny_time_source_t` | Monotonic clock |

For protocol details, see the [tiny-gea-api](https://github.com/geappliances/tiny-gea-api) repository.