# MQTT Data Publishing Specification

This document defines requirements for MQTT data publishing in the GE Appliances bridge.

## Specification 1: Hexadecimal ERD Data Format

### Requirement

All ERD data published to MQTT topics under `geappliances/<deviceId>/erd/0x<ERD>/value` must be in hexadecimal format.

### Rationale

- Raw binary data is not MQTT-safe; hex encoding ensures reliable transport
- String conversion belongs at the consumer level, not the transport level
- Consistent format simplifies downstream processing and testing

### Implementation

The ERD cache MQTT publisher (`erd_cache_mqtt_publisher.cpp`) converts all ERD values to lowercase hex strings before publishing.

### Example

| ERD | Raw Bytes | Published Payload |
|-----|-----------|-------------------|
| 0x0001 (Model Number) | `4a 45 53 39 35 30 30 53 53 53 00 00` | `4a4553393530305353530000` |
| 0x0002 (Serial Number) | `41 56 56 4c 32 34 44 4d 58 58 41 4b 31 00` | `4156564c3234444d5858414b3100` |

### Prohibited

- Publishing raw binary bytes directly to MQTT
- Converting ERD data to ASCII strings at the MQTT adapter level
- Maintaining a list of "string-type" ERDs for special handling in the MQTT adapter

### Verification

Tests must verify that published payloads are valid hex strings (only characters `0-9a-f`).

## Specification 2: No Full Re-publish After MQTT Reconnect

### Requirement

After an MQTT disconnect/reconnect, the publisher resumes publishing only entries whose `update_required` flag is `true`. It does **not** re-mark all cached ERDs for re-publish.

### Rationale

- MQTT messages are published with `retain = true`. On client reconnect to a running broker, retained messages persist — Home Assistant re-subscribes and receives them automatically.
- A full re-publish after every reconnect would cause a burst of MQTT traffic proportional to the number of cached ERDs (potentially 200+), unnecessarily loading the broker and network.
- The only scenario where retained messages are lost is a **broker restart** (not a client reconnect). In that case, Home Assistant entities may show stale data until the appliance's state changes and triggers a new update.
- This is an acceptable tradeoff: broker restarts are rare, and the stale data window is bounded by the next appliance state change.

### Design Decision

The `on_connected()` callback sets `mqtt_connected = true` and resumes normal publishing. It does not call any function to re-mark all cache entries. If full re-publish after reconnect is desired in the future, add a `erd_cache_mark_all_updated()` function and call it from `on_connected()`.

### Verification

Tests verify that after a simulated disconnect/reconnect cycle, only entries with new data changes are published — not a full cache flush.
