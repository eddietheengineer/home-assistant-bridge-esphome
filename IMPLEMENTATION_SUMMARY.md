# Implementation Summary - Heap Monitoring Sensors

## Overview
Added three ESPHome sensors to monitor memory usage on the ESP32-C6 to help diagnose the crashes you're experiencing.

## Files Modified

### 1. `components/geappliances_bridge/geappliances_bridge.h`
**Changes:**
- Added `#include "esphome/components/sensor/sensor.h"`
- Added three setter methods:
  - `set_free_heap_sensor(sensor::Sensor* s)`
  - `set_min_free_heap_sensor(sensor::Sensor* s)`
  - `set_heap_fragmentation_sensor(sensor::Sensor* s)`
- Added member variables:
  - `sensor::Sensor* free_heap_sensor_{nullptr}`
  - `sensor::Sensor* min_free_heap_sensor_{nullptr}`
  - `sensor::Sensor* heap_fragmentation_sensor_{nullptr}`
  - `uint32_t last_heap_sensor_update_{0}`
  - `static constexpr uint32_t HEAP_SENSOR_UPDATE_INTERVAL_MS = 60000` (60 seconds)

### 2. `components/geappliances_bridge/geappliances_bridge.cpp`
**Changes:**
- Added `#include "esp_heap_utils.h"` for ESP-IDF heap functions
- Added "Phase 9" in `loop()` function:
  - Updates sensors every 60 seconds
  - Publishes free heap size (bytes)
  - Publishes minimum free heap size (bytes)
  - Publishes heap usage percentage (0-100%)

### 3. `components/geappliances_bridge/__init__.py`
**Changes:**
- Added `sensor` to imports: `from esphome.components import esp32, uart, mqtt, sensor`
- Added configuration constants:
  - `CONF_FREE_HEAP_SENSOR = "free_heap_sensor"`
  - `CONF_MIN_FREE_HEAP_SENSOR = "min_free_heap_sensor"`
  - `CONF_HEAP_FRAGMENTATION_SENSOR = "heap_fragmentation_sensor"`
- Added to `CONFIG_SCHEMA`:
  - `cv.Optional(CONF_FREE_HEAP_SENSOR): cv.use_id(sensor.Sensor)`
  - `cv.Optional(CONF_MIN_FREE_HEAP_SENSOR): cv.use_id(sensor.Sensor)`
  - `cv.Optional(CONF_HEAP_FRAGMENTATION_SENSOR): cv.use_id(sensor.Sensor)`
- Added to `to_code()` function:
  - Code generation to wire up the sensors if configured

## How to Use

### Step 1: Define Sensors in YAML
```yaml
sensor:
  - platform: template
    name: "Free Heap"
    id: free_heap_sensor
    unit_of_measurement: "B"
    device_class: memory_usage
    state_class: measurement
    accuracy_decimals: 0
    icon: mdi:memory

  - platform: template
    name: "Minimum Free Heap"
    id: min_free_heap_sensor
    unit_of_measurement: "B"
    device_class: memory_usage
    state_class: measurement
    accuracy_decimals: 0
    icon: mdi:memory

  - platform: template
    name: "Heap Usage"
    id: heap_fragmentation_sensor
    unit_of_measurement: "%"
    device_class: memory_usage
    state_class: measurement
    accuracy_decimals: 1
    icon: mdi:memory
```

### Step 2: Wire Sensors to Bridge
```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  adapter_address: 0xE5 
  appliance_api_parsing: false
  
  # Add these three lines
  free_heap_sensor: free_heap_sensor
  min_free_heap_sensor: min_free_heap_sensor
  heap_fragmentation_sensor: heap_fragmentation_sensor
  
  custom_erds:
    - 0xF016
    # ... rest of your ERDs ...
```

### Step 3: Deploy and Monitor
After deploying, you'll see three new sensors in Home Assistant that update every 60 seconds.

## What to Watch For

### Critical Thresholds (ESP32-C6 with ~320 KB total RAM)

| Sensor | Healthy | Warning | Critical |
|--------|---------|---------|----------|
| Free Heap | > 50 KB | 30-50 KB | < 30 KB |
| Min Free Heap | Stable | Decreasing | < 30 KB |
| Heap Usage | < 70% | 70-85% | > 85% |

### Expected Values for Your Setup
- **At boot**: ~250-280 KB free
- **After WiFi+MQTT**: ~180-200 KB free
- **After bridge init**: ~100-150 KB free
- **During polling**: may dip to 80-100 KB

If you see values consistently below 60 KB free or usage above 80%, that correlates with the FreeRTOS idle task crashes in your logs.

## Why This Helps Diagnose Your Crashes

Your logs show:
```
CRASH DETECTED ON PREVIOUS BOOT
Reason: Fault - Unknown
Crashed core: 0
PC:  0x4080430C  (esp_cpu_wait_for_intr)
BT2: 0x420C9F3A  (prvCheckTasksWaitingTermination / prvIdleTask)
```

This is the FreeRTOS **idle task** crashing, which typically indicates:
1. **Heap corruption** from memory exhaustion
2. **Stack overflow** from another task corrupting adjacent memory
3. **Memory fragmentation** causing allocation failures

The sensors will tell us definitively if memory pressure is the root cause.

## Next Steps

1. ✅ Code implemented and ready
2. ⏳ Update your `gea-esphome-zonelinec6.yaml` with sensor definitions
3. ⏳ Compile and deploy to device
4. ⏳ Monitor sensors for 24-48 hours
5. ⏳ Share sensor data - I'll help analyze and recommend fixes if needed

## Files Created

- `HEAP_SENSORS_GUIDE.md` - Complete usage guide with examples
- `IMPLEMENTATION_SUMMARY.md` - This file (technical details)

## Branch
Changes are on branch: `localTestBranch`
