# Interface Reference

Key C interfaces used within the bridge component.

## i_mqtt_client

Abstract MQTT client interface. Decouples bridge modules from ESPHome's MQTT implementation.

**File:** `components/geappliances_bridge/i_mqtt_client.h`

| Method | Description |
|---|---|
| `register_erd()` | Register an ERD for MQTT publishing |
| `publish_erd()` | Publish an ERD value to MQTT |
| `update_erd_write_result()` | Report write success/failure to MQTT |

**Implementation:** `EsphomeMqttClientAdapter` bridges to ESPHome's `global_mqtt_client`.

**Event:** `mqtt_client_on_write_request` — fired when a write command arrives on a `*/write` topic. Subscribed by `ErdWriteBridge`.

## IBridgeServices

Abstract interface between the startup HSM and the bridge. The HSM calls methods on this interface to drive startup phases without depending on the concrete `GeappliancesBridge` class.

**File:** `components/geappliances_bridge/i_bridge_services.h`

| Method | Description |
|---|---|
| `start_autodiscovery()` | Begin appliance discovery |
| `start_device_identity()` | Read identity ERDs |
| `start_feature_bits()` | Read feature bit ERDs |
| `init_mqtt_client()` | Initialize MQTT adapter with device ID |
| `init_bridge()` | Initialize polling/subscription bridge |
| `is_phase_complete()` | Query if a startup phase finished |
| `loop()` | Drive ongoing work in main loop |

**Implementation:** `GeappliancesBridge` implements this interface.

## i_tiny_gea3_erd_client_t

GEA3 ERD client interface from the `tiny-gea-api` submodule.

| Method | Description |
|---|---|
| `read()` | Read an ERD from the appliance |
| `write()` | Write data to an ERD |
| `subscribe()` | Subscribe to ERD publications |
| `retain_subscription()` | Retain active subscriptions |

**Event:** `on_activity` — fired on read/write/subscribe completion.

## i_tiny_gea2_erd_client_t

GEA2 ERD client interface from the `tiny-gea-api` submodule. Same methods as GEA3, but `subscribe()` and `retain_subscription()` are not supported.

## i_tiny_uart_t

UART HAL interface from the `tiny` submodule. The `EsphomeUartAdapter` implements this to bridge ESPHome's `UARTComponent` to the GEA protocol stack.

| Method | Description |
|---|---|
| `send()` | Transmit bytes |
| `on_receive()` | Event callback for received data |

## i_tiny_time_source_t

Monotonic clock interface. The `EsphomeTimeSource` implements this using ESPHome's `millis()`.

| Method | Description |
|---|---|
| `now()` | Current time in milliseconds |