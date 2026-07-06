# home-assistant-bridge-esphome

ESPHome external component bridging GE Appliances (GEA2/GEA3 serial protocols) to Home Assistant via MQTT.

The component discovers the appliance on the serial bus, reads its identity, determines which ERDs (Entity-Relationship Data points) are available, and continuously publishes those values to MQTT while accepting write commands from Home Assistant.

## Quick Start

### Hardware

This component runs on the **FirstBuild Home Assistant Adapter** (SeeedStudio Xiao ESP32-C3 or ESP32-C6) with an RJ45 connection for GEA3 serial communication.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/). See [HARDWARE.md](./HARDWARE.md) for pin configuration and troubleshooting.

### Configuration

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

For a complete configuration with all options, see [doc/example.yaml](./doc/example.yaml).

### How It Works

1. **Autodiscovery** — The bridge broadcasts on the GEA bus to find the appliance address and protocol (GEA3 or GEA2).
2. **Identity** — Reads appliance type, model, and serial number to generate a device ID (e.g., `Dishwasher_ZL4200ABC_12345678`).
3. **Feature bits** — Reads which ERDs the appliance supports.
4. **Data bridge** — Subscribes to ERD updates (or polls if subscriptions are unavailable) and publishes values to MQTT.
5. **Home Assistant discovery** — Generates HA entities from embedded ERD definitions.

## Configuration Options

| Parameter | Default | Description |
|---|---|---|
| `gea3_uart_id` | *(required)* | ESPHome UART ID for GEA3 communication |
| `gea2_uart_id` | *(optional)* | ESPHome UART ID for GEA2 (older appliances) |
| `device_id` | auto-generated | Custom device ID; auto format: `Type_Model_Serial` |
| `mode` | `auto` | `auto`, `subscribe`, or `poll` |
| `polling_interval` | `10000` ms | Poll interval in milliseconds (poll mode) |
| `appliance_api_parsing` | `true` | Restrict polling to appliance-supported ERDs |
| `throttle_rate_seconds` | `1` | Min seconds between publishes per ERD (0–255, 0=disabled) |
| `filter_config_topics` | `true` | Filter internal/diagnostic entities from HA discovery |
| `adapter_address` | `0xE4` | Bridge's address on the GEA bus |
| `custom_erds` | none | Additional ERD IDs to poll |

### Mode

| Mode | Behavior |
|---|---|
| `auto` (default) | Starts with subscription; falls back to polling if no responses within 10s |
| `subscribe` | Appliance pushes ERD changes as they occur |
| `poll` | Bridge reads ERDs at `polling_interval` |

For detailed configuration guidance per appliance type, see the [Deployment Guide](./docs/guides/deployment.md).

## MQTT Topics

| Topic | Direction | Payload |
|---|---|---|
| `geappliances/{device_id}/erd/0x{ERD}/value` | Bridge → HA | Hex-encoded ERD data |
| `geappliances/{device_id}/erd/0x{ERD}/write` | HA → Bridge | Hex bytes to write |
| `geappliances/{device_id}/erd/0x{ERD}/write_result` | Bridge → HA | `success` or `failure (reason: N)` |

See [MQTT Topics Reference](./docs/reference/mqtt-topics.md) for the complete topic schema.

## Documentation

| Resource | Description |
|---|---|
| [Documentation Index](./docs/README.md) | Full documentation navigation |
| [Architecture](./docs/architecture/overview.md) | System context, data flow, module diagrams |
| [Specifications](./docs/spec/) | Detailed behavioral contracts per module |
| [Hardware Guide](./HARDWARE.md) | Pin configuration, wiring, troubleshooting |
| [Quickstart](./docs/guides/quickstart.md) | Get up and running in 5 minutes |
| [Deployment Guide](./docs/guides/deployment.md) | Configuration per appliance type |
| [Troubleshooting](./docs/guides/troubleshooting.md) | Common issues and diagnostic flow |
| [Development Guide](./docs/guides/development.md) | Build, test, debug workflow |
| [Pipeline Guide](./docs/guides/pipeline.md) | HA discovery pipeline end-to-end |
| [YAML Reference](./docs/reference/yaml-config.md) | All configuration options |

## Development

```bash
git clone --recursive https://github.com/eddietheengineer/home-assistant-bridge-esphome.git
cd home-assistant-bridge-esphome
make test        # Run unit tests
make pytest      # Run Python tests
```

See [CONTRIBUTING.md](./CONTRIBUTING.md) for the full development workflow.

## License

MIT — see [LICENSE](./LICENSE).
