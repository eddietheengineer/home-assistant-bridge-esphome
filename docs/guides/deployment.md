# Deployment Guide

Configuration examples for specific appliance types and deployment scenarios.

## Appliance Types

### Dishwasher

```yaml
esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
  framework:
    type: esp-idf

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  discovery: true

uart:
  - id: gea3_uart
    tx_pin: GPIO21
    rx_pin: GPIO20
    baud_rate: 230400

geappliances_bridge:
  gea3_uart_id: gea3_uart
  mode: auto
  appliance_api_parsing: true
  throttle_rate_seconds: 1
  filter_config_topics: true
```

**Notes:** Dishwashers typically support GEA3 subscriptions. Auto mode is recommended — it starts with subscription and falls back to polling if needed.

### Refrigerator

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  mode: auto
  appliance_api_parsing: true
  throttle_rate_seconds: 2  # Refrigerators update less frequently
```

**Notes:** Refrigerators have many ERDs (temperature zones, door states, ice maker). The `throttle_rate_seconds: 2` reduces MQTT traffic. Entity count is typically 800–1200.

### Range / Oven

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  mode: auto
  appliance_api_parsing: true
```

**Notes:** Ranges have cycle state, temperature, and timer ERDs. Some models support subscription, others require polling.

### Washer / Dryer

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  mode: auto
  appliance_api_parsing: true
```

**Notes:** Laundry appliances have cycle progress, remaining time, and option ERDs. Cycle state changes are the most frequent updates.

### Water Heater

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  mode: poll
  polling_interval: 30000  # 30 seconds — water heaters change slowly
  appliance_api_parsing: true
```

**Notes:** Water heaters typically don't support GEA3 subscriptions. Use poll mode with a longer interval to reduce bus traffic.

### Air Conditioning / HVAC

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  mode: auto
  appliance_api_parsing: true
  throttle_rate_seconds: 1
```

**Notes:** HVAC units have many diagnostic ERDs. `filter_config_topics: true` (default) removes internal diagnostic entities from Home Assistant discovery.

## GEA2 (Legacy Appliances)

For older appliances using the GEA2 protocol, configure both UARTs:

```yaml
uart:
  - id: gea3_uart
    tx_pin: GPIO21
    rx_pin: GPIO20
    baud_rate: 230400

  - id: gea2_uart
    tx_pin: GPIO9
    rx_pin: GPIO10
    baud_rate: 19200
    rx_full_threshold: 1   # required for GEA2
    rx_timeout: 1          # required for GEA2

geappliances_bridge:
  gea3_uart_id: gea3_uart
  gea2_uart_id: gea2_uart
  mode: poll
  polling_interval: 10000
```

**Important:** The `rx_full_threshold: 1` and `rx_timeout: 1` settings on the GEA2 UART are required for reliable communication. See [HARDWARE.md](../../HARDWARE.md) for details.

## ESP32-C6 Deployment

For the ESP32-C6 variant (more SRAM, better for large appliances):

```yaml
esp32:
  board: seeed_xiao_esp32c6
  variant: esp32c6
  framework:
    type: esp-idf
```

**Notes:** The ESP32-C6 has 512 KB SRAM vs. 320 KB on the C3, providing more headroom during Home Assistant discovery for appliances with many ERDs.

## Custom Device ID

To use a fixed device ID instead of auto-generation:

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  device_id: "MyDishwasher"
```

The device ID is used in MQTT topics: `geappliances/MyDishwasher/erd/0x0008/value`.

## Custom ERDs

To poll ERDs not in the standard list:

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  custom_erds:
    - 0x7100
    - 0x7101
```

## Production Deployment Checklist

- [ ] Verify appliance is powered on and the RJ45 cable is firmly seated.
- [ ] Confirm WiFi credentials in `secrets.yaml` are correct.
- [ ] Confirm MQTT broker address and credentials are correct.
- [ ] Set `logger: { level: INFO }` for production (use DEBUG only for troubleshooting).
- [ ] Test with `esphome run` and verify entities appear in Home Assistant.
- [ ] Check ESPHome logs for any errors during startup.
- [ ] Verify the device appears in Home Assistant **Devices** panel.
- [ ] Spot-check a few entities (cycle state, temperature) for correct values.