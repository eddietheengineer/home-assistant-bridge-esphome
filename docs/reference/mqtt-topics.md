# MQTT Topics Reference

Complete reference for all MQTT topics used by the GE Appliances Bridge.

## Data Topics

### ERD Value

| Property | Value |
|---|---|
| **Topic** | `geappliances/{device_id}/erd/0x{ERD_ID:04X}/value` |
| **Direction** | Bridge → Broker (published by bridge) |
| **QoS** | 0 |
| **Retain** | `true` |
| **Payload** | Lowercase hex string of the raw ERD bytes |

**Example:**

```
Topic: geappliances/Dishwasher_ZL4200ABC_12345678/erd/0x0008/value
Payload: 06
```

The `0x0008` ERD (appliance type) has value `06` (Dishwasher), published as the hex string `06`.

### ERD Write

| Property | Value |
|---|---|
| **Topic** | `geappliances/{device_id}/erd/0x{ERD_ID:04X}/write` |
| **Direction** | Client → Bridge (subscribed by bridge via wildcard) |
| **QoS** | 0 |
| **Retain** | `false` |
| **Payload** | Lowercase hex string of the bytes to write |

**Example:**

```
Topic: geappliances/Dishwasher_ZL4200ABC_12345678/erd/0x7001/write
Payload: 01
```

This writes `0x01` to ERD `0x7001`.

### ERD Write Result

| Property | Value |
|---|---|
| **Topic** | `geappliances/{device_id}/erd/0x{ERD_ID:04X}/write_result` |
| **Direction** | Bridge → Broker (published by bridge) |
| **QoS** | 0 |
| **Retain** | `false` |
| **Payload** | `success` or `failure (reason: {N})` |

**Example (success):**

```
Topic: geappliances/Dishwasher_ZL4200ABC_12345678/erd/0x7001/write_result
Payload: success
```

**Example (failure):**

```
Topic: geappliances/Dishwasher_ZL4200ABC_12345678/erd/0x7001/write_result
Payload: failure (reason: 2)
```

Failure reasons are defined by the GEA protocol stack.

## Home Assistant Discovery Topics

### Discovery Config

| Property | Value |
|---|---|
| **Topic** | `homeassistant/{domain}/{device_id}/{entity_id}/config` |
| **Direction** | Bridge → Broker (published by bridge) |
| **QoS** | 0 |
| **Retain** | `true` |
| **Payload** | JSON discovery payload per [HA MQTT Discovery spec](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery) |

**Example:**

```
Topic: homeassistant/sensor/Dishwasher_ZL4200ABC_12345678/cycle_state/config
Payload: {"~": "geappliances/Dishwasher_ZL4200ABC_12345678", "name": "Cycle State", "state_topic": "~/erd/0x7001/value", ...}
```

### Discovery Cleanup

During discovery cleanup, the bridge republishes discovery topics with an empty retained payload to remove them from the broker:

| Property | Value |
|---|---|
| **Topic** | `homeassistant/{domain}/{device_id}/{entity_id}/config` |
| **Payload** | `""` (empty string, retained) |

## Topic Patterns

The bridge subscribes to the following wildcard topic to receive write commands:

```
geappliances/{device_id}/erd/+/write
```

The `+` wildcard matches any single topic level (the ERD ID).

## Device ID in Topics

The `{device_id}` placeholder is either:
- The value of the `device_id` configuration parameter, or
- Auto-generated as `{ApplianceType}_{ModelNumber}_{SerialNumber}` (e.g., `Dishwasher_ZL4200ABC_12345678`)

The device ID is URL-safe: spaces and special characters are replaced with underscores.

## Payload Encoding

All ERD data payloads are lowercase hexadecimal strings with no prefix, no spaces, and no separators:

| Raw Bytes | Payload |
|---|---|
| `06` | `06` |
| `4a 45 53 39` | `4a455339` |
| `00 00 ff 01` | `0000ff01` |