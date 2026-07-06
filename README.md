# home-assistant-bridge-esphome

ESPHome external component bridging GE Appliances (GEA2/GEA3 serial protocols) to Home Assistant via MQTT.

## Hardware

This component runs on the **FirstBuild Home Assistant Adapter** (SeeedStudio Xiao ESP32-C3 or ESP32-C6) with an RJ45 connection for GEA3 serial communication.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/).

## Configuration

Add to your ESPHome YAML:

```yaml
esp32:
  board: seeed_xiao_esp32c3
  variant: esp32c3
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
    tx_pin: GPIO21
    rx_pin: GPIO20
    baud_rate: 230400

geappliances_bridge:
  gea3_uart_id: gea3_uart
```

For a complete configuration with all options, see [docs/example.yaml](./docs/example.yaml).

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
| [Quickstart](./docs/guides/quickstart.md) | Get up and running in 5 minutes |
| [Deployment Guide](./docs/guides/deployment.md) | Configuration per appliance type |
| [Troubleshooting](./docs/guides/troubleshooting.md) | Common issues and fixes |
| [Hardware Guide](./HARDWARE.md) | Pin configuration and wiring |
| [YAML Reference](./docs/reference/yaml-config.md) | All configuration options |
| [MQTT Topics](./docs/reference/mqtt-topics.md) | Topic schema and payloads |
| [Architecture](./docs/architecture/overview.md) | System design and data flow |
| [Contributing](./CONTRIBUTING.md) | Development workflow |

## License

MIT — see [LICENSE](./LICENSE).
