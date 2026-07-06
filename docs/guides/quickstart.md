# Quickstart

Get the GE Appliances Bridge running on your FirstBuild Home Assistant Adapter in 5 minutes.

## Prerequisites

- **FirstBuild Home Assistant Adapter** (Xiao ESP32-C3 or ESP32-C6)
- **GE Appliance** with GEA3 (or GEA2) serial port
- **Home Assistant** instance with MQTT integration configured
- **Ethernet cable** (RJ45) to connect the adapter to the appliance

## Step 1: Wire the Hardware

1. Connect the FirstBuild adapter to your GE appliance's GEA3 port using an Ethernet cable.
2. Power the adapter via USB-C (5V).
3. Connect the adapter to the same network as your Home Assistant instance.

## Step 2: Create ESPHome Configuration

Create a file `gea-bridge.yaml`:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
  framework:
    type: esp-idf

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "GEA Bridge Fallback"
    password: !secret ap_password

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  discovery: true
  discovery_prefix: homeassistant

uart:
  - id: gea3_uart
    tx_pin: GPIO21
    rx_pin: GPIO20
    baud_rate: 230400

geappliances_bridge:
  gea3_uart_id: gea3_uart
```

Create `secrets.yaml` in the same directory:

```yaml
wifi_ssid: "YourNetworkName"
wifi_password: "YourWiFiPassword"
mqtt_broker: "192.168.1.x"
mqtt_username: "your_mqtt_user"
mqtt_password: "your_mqtt_password"
ap_password: "FallbackAPPassword"
```

## Step 3: Build and Flash

```bash
esphome run gea-bridge.yaml
```

The build process:
1. Fetches the external component from GitHub.
2. Compiles the firmware with ESP-IDF.
3. Flashes to the adapter over USB.

## Step 4: Verify

After flashing, the adapter boots and goes through startup:

1. **Autodiscovery** — broadcasts on the GEA bus (~5 seconds).
2. **Identity read** — reads appliance type, model, serial number.
3. **Feature bits** — determines which ERDs are supported.
4. **Bridge init** — starts subscription or polling.
5. **Discovery** — publishes Home Assistant entities.

Check the ESPHome logs for progress:

```
[INFO] geappliances_bridge: Autodiscovery found appliance at address 0xC0
[INFO] geappliances_bridge: Device ID: Dishwasher_ZL4200ABC_12345678
[INFO] geappliances_bridge: Subscription bridge active
[INFO] geappliances_bridge: Published 1247 discovery topics
```

In Home Assistant, check **Devices** — you should see a new GE Appliances device with entities for cycle state, temperature, door state, etc.

## Next Steps

- [Deployment Guide](./deployment.md) — Configuration for specific appliance types.
- [Troubleshooting](./troubleshooting.md) — Common issues and fixes.
- [Architecture](../architecture/overview.md) — How the bridge works internally.