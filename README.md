# home-assistant-bridge-esphome

ESPHome external component bridging GE Appliances (GEA2/GEA3 serial protocols) to Home Assistant via MQTT.

## Hardware

This component runs on the **FirstBuild Home Assistant Adapter** (SeeedStudio Xiao ESP32-C3) with an RJ45 connection for GEA3 serial communication.

Available from [FirstBuild](https://firstbuild.com/inventions/home-assistant-adapter/).

## Quick Start

Make sure that [ESPHome Device Builder](https://esphome.io/guides/getting_started_hassio/) and the [MQTT Broker](https://www.home-assistant.io/integrations/mqtt/) has been set up on your Home Assistant installation.

In ESPHome Device Builder, click the three dots in the upper right and select Secrets:

<img width="225" height="370" alt="Screenshot 2026-07-09 at 8 03 35 AM" src="https://github.com/user-attachments/assets/86a526b6-4b9f-4346-b515-f889d22a5ccb" />

In this window, make sure the following fields are added and filled out according to your configuration:


```
wifi_ssid: "TestSSID"
wifi_password: "TestPassword123"
mqtt_broker: "192.168.1.100"
mqtt_username: "test_user"
mqtt_password: "test_password"
api_encryption_key: "QLeW3ueZeYgKFtgAUnWAroo9DwDko/aIqYcblAX/9vs="
```

On the ESPHome Device Builder main screen, select Add Device in the lower right hand corner, and then select Advanced Options, and Empty Configuration: 

<img width="519" height="598" alt="Screenshot 2026-07-09 at 8 05 55 AM" src="https://github.com/user-attachments/assets/a868ec94-001c-4ed9-9448-8f93259bdf52" />

Fill out the device name, and then copy and paste the following into the window. Change the name and friendly name to be unique if you will be using multiple adapters (you can match the device name you specified in the previous step):

```yaml
substitutions:
  name: gea-esphome
  friendly_name: gea-esphome

esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  name_add_mac_suffix: false

api:
  encryption:
    key: !secret api_encryption_key

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# Enable logging
logger:
  baud_rate: 0

# Allow OTA updates
ota:
  - platform: esphome

esp32:
  board: seeed_xiao_esp32c3
  framework:
    type: esp-idf

# External component configuration
external_components:
  - source: github://eddietheengineer/home-assistant-bridge-esphome@develop
    components: [ geappliances_bridge ]

# MQTT configuration for Home Assistant
mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  port: 1883
  discovery: true
  discovery_prefix: homeassistant

# UART configuration
uart:
  # GEA3 UART (newer appliances)
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

# GE Appliances Bridge component
geappliances_bridge:
  gea3_uart_id: gea3_uart
  gea2_uart_id: gea2_uart
  # device_id: "YourDeviceId"             # Optional: Uncomment to use a custom device ID
  # mode: auto                            # Default:  auto   Options: auto, subscribe, poll
  # polling_interval: 10000               # Default:  10000 ms (10 seconds), used when in polling mode
  # throttle_rate_seconds: 1              # Default:  1, minimum seconds between publishes per ERD (0-255, 0=disabled)
  # appliance_api_parsing: true           # Default:  true, restricts polling to appliance-supported ERDs
  # generate_device_config: true          # Default:  true, generates MQTT Autodiscovery payloads (experimental)
  # filter_config_topics: true            # Default:  true, filters internal/diagnostic entities from HA discovery
```

Select Install in the lower right corner, and "Plug into this computer" to generate a firmware file.

<img width="454" height="371" alt="Screenshot 2026-07-09 at 8 08 41 AM" src="https://github.com/user-attachments/assets/180e92d0-9537-44e2-badb-96f531d39efe" />

Once it is compiled, navigate to [ESPHome Web](https://web.esphome.io) on a supported browser such as Chrome. Plug in the adapter to your computer via USB, and then flash the compiled firmware to your device. Once it has completed flashing, disconnect the adapter from your computer and connect the adapter to your appliance using a standard Ethernet cable. You should see the adapter show up on your ESPHome dashboard as "Online" within a minute!

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
