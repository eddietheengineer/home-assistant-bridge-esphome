# MQTT Data Publishing Specification

This document defines requirements for MQTT data publishing in the GE Appliances bridge.

## Specification 1: Hexadecimal ERD Data Format

### Requirement

All ERD data published to MQTT topics under `geappliances/<deviceId>/erd/0x<ERD>/value` must be in hexadecimal format.

### Rationale

- Raw binary data is not MQTT-safe; hex encoding ensures reliable transport
- String conversion belongs at the consumer level (HA discovery), not the transport level
- Consistent format simplifies downstream processing and testing

### Implementation

The MQTT client adapter (`esphome_mqtt_client_adapter.cpp`) must convert all ERD values to lowercase hex strings before publishing. The `update_erd()` function is the single point of conversion.

### Example

| ERD | Raw Bytes | Published Payload |
|-----|-----------|-------------------|
| 0x0001 (Model Number) | `4A 45 53 39 35 30 30 53 53 53 00 00` | `4a4553393530305353530000` |
| 0x0002 (Serial Number) | `41 56 56 4C 32 34 44 4D 58 58 41 4B 31 00` | `4156564c3234444d5858414b3100` |

### Prohibited

- Publishing raw binary bytes directly to MQTT
- Converting ERD data to ASCII strings at the MQTT adapter level
- Maintaining a list of "string-type" ERDs for special handling in the MQTT adapter

### Verification

Tests must verify that published payloads are valid hex strings (only characters `0-9a-f`).
