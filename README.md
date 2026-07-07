# home-assistant-bridge-esphome

ESPHome external component bridging GE Appliances (GEA2/GEA3 serial protocols) to Home Assistant via MQTT.

## Hardware

This component runs on the **FirstBuild Home Assistant Adapter** (SeeedStudio Xiao ESP32-C3 or ESP32-C6) with an RJ45 connection for GEA3 serial communication.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/).

## Quick Start

Get the GE Appliances Bridge running in 5 minutes.

### Prerequisites

- **FirstBuild Home Assistant Adapter** (Xiao ESP32-C3)
- **GE Appliance** with GEA3 (or GEA2) serial port
- **Home Assistant** instance with MQTT integration configured
- **Ethernet cable** (RJ45) to connect the adapter to the appliance

### Step 1: Wire the Hardware

1. Connect the FirstBuild adapter to your GE appliance's GEA3 port using an Ethernet cable.
2. Power the adapter via USB-C (5V).
3. Connect the adapter to the same network as your Home Assistant instance.

### Step 2: Create ESPHome Configuration

You can create the configuration either through the **ESPHome Dashboard** in Home Assistant or via the command line.

#### Option A: ESPHome Dashboard (Recommended)

1. In Home Assistant, navigate to **Settings** → **Devices & Services** → **ESPHome** → **Add Device** → **New Device**.
2. Enter a device name (e.g., `gea-bridge`) and click **Next**.
3. Click **Edit** to open the YAML editor.
4. Replace the default content with the configuration below, selecting the section matching your appliance protocol.
5. Click **Save**, then click **Install**. Select a download method (e.g., **Download UF2** for flashing via USB) and follow the prompts.
6. Once installed, the device will appear in the ESPHome dashboard. Click **Logs** to monitor startup.

#### Option B: Command Line

Create a file `gea-bridge.yaml` with the configuration below, then run:

```bash
esphome run gea-bridge.yaml
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

### Step 3: Configuration

Choose the configuration matching your appliance's serial protocol.

#### GEA3 (Newer Appliances)

```yaml
esp32:
  board: seeed_xiao_esp32c3
  framework:
    type: esp-idf

external_components:
  - source: github://eddietheengineer/home-assistant-bridge-esphome@develop
    components: [ geappliances_bridge ]

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  discovery: true

uart:
  - id: gea3_uart
    tx_pin: GPIO21  # D6 on Xiao ESP32-C3
    rx_pin: GPIO20  # D7 on Xiao ESP32-C3
    baud_rate: 230400

geappliances_bridge:
  gea3_uart_id: gea3_uart
```

#### GEA2 (Older Appliances)

GEA2 appliances communicate at 19200 baud. The `rx_full_threshold` and `rx_timeout` settings are **required** for reliable communication:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  framework:
    type: esp-idf

external_components:
  - source: github://eddietheengineer/home-assistant-bridge-esphome@develop
    components: [ geappliances_bridge ]

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  discovery: true

uart:
  - id: gea2_uart
    tx_pin: GPIO9   # D9 on Xiao ESP32-C3
    rx_pin: GPIO10  # D10 on Xiao ESP32-C3
    baud_rate: 19200
    rx_full_threshold: 1  # required: deliver each byte immediately
    rx_timeout: 1         # required: minimise idle-flush latency

geappliances_bridge:
  gea2_uart_id: gea2_uart
```

#### GEA2 + GEA3 (Both Protocols)

For setups needing both interfaces simultaneously:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  framework:
    type: esp-idf

external_components:
  - source: github://eddietheengineer/home-assistant-bridge-esphome@develop
    components: [ geappliances_bridge ]

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  discovery: true

uart:
  - id: gea3_uart
    tx_pin: GPIO21  # D6 on Xiao ESP32-C3
    rx_pin: GPIO20  # D7 on Xiao ESP32-C3
    baud_rate: 230400

  - id: gea2_uart
    tx_pin: GPIO9   # D9 on Xiao ESP32-C3
    rx_pin: GPIO10  # D10 on Xiao ESP32-C3
    baud_rate: 19200
    rx_full_threshold: 1
    rx_timeout: 1

geappliances_bridge:
  gea3_uart_id: gea3_uart
  gea2_uart_id: gea2_uart
```

For a complete configuration with all options, see [docs/example.yaml](./docs/example.yaml).

### Step 4: Verify

After flashing, the adapter boots and goes through startup:

1. **Autodiscovery** — broadcasts on the GEA bus (~5 seconds).
2. **Identity read** — reads appliance type, model, serial number.
3. **Feature bits** — determines which ERDs are supported.
4. **Bridge init** — starts subscription or polling.
5. **Discovery** — publishes Home Assistant entities.

Check the ESPHome logs for progress. Example output:

```
[INFO] geappliances_bridge: Autodiscovery found appliance at address 0xC0
[INFO] geappliances_bridge: Device ID: Dishwasher_ZL4200ABC_12345678
[INFO] geappliances_bridge: Subscription bridge active
[INFO] geappliances_bridge: Published 1247 discovery topics
```

*Actual log output will vary by appliance type and configuration.*

In Home Assistant, check **Devices** — you should see a new GE Appliances device with entities for cycle state, temperature, door state, etc.

## Home Assistant Discovery

HA discovery topics are published by the bridge and retained on the MQTT broker.
On normal boots, discovery is skipped — the broker retains the topics from the
previous session. Discovery runs in two scenarios:

- **After OTA reboot:** The bridge detects an OTA reboot, cleans old discovery
  topics, publishes fresh ones, and reboots to defragment the heap.
- **Discovery Refresh button:** Manually triggers cleanup of stale topics,
  republishes fresh discovery, and reboots.

## Documentation

| Resource | Description |
|---|---|
| [Deployment Guide](./docs/guides/deployment.md) | Configuration per appliance type |
| [Troubleshooting](./docs/guides/troubleshooting.md) | Common issues and fixes |
| [Hardware Guide](./HARDWARE.md) | Pin configuration and wiring |
| [YAML Reference](./docs/reference/yaml-config.md) | All configuration options |
| [MQTT Topics](./docs/reference/mqtt-topics.md) | Topic schema and payloads |
| [Architecture](./docs/architecture/overview.md) | System design and data flow |
| [Contributing](./CONTRIBUTING.md) | Development workflow |

## License

MIT — see [LICENSE](./LICENSE).