# Heap Monitoring Sensors - Implementation Complete

## Summary

I've successfully added three heap monitoring sensors to the `geappliances_bridge` component that will help diagnose the crashes you're experiencing.

## What Was Added

### C++ Changes (`geappliances_bridge.h` and `.cpp`)
- Added `sensor::Sensor` pointers for three sensors
- Added timer to update sensors every 60 seconds
- Added `esp_heap_utils.h` include for heap functions
- Added logic in `loop()` to publish:
  - **Free Heap** - Current available heap in bytes
  - **Minimum Free Heap** - Lowest free heap since boot in bytes
  - **Heap Fragmentation** - Percentage of heap used (0-100%)

### Python Changes (`__init__.py`)
- Added sensor import
- Added three new config options:
  - `free_heap_sensor`
  - `min_free_heap_sensor`
  - `heap_fragmentation_sensor`
- Added code generation to wire sensors to the component

## How to Use

### Step 1: Define the sensors in your YAML

```yaml
sensor:
  # Existing uptime sensor
  - platform: uptime
    type: seconds
    name: Uptime Sensor

  # NEW: Heap monitoring sensors
  - platform: template
    name: "Free Heap"
    id: free_heap_sensor
    unit_of_measurement: "B"
    state_class: measurement
    accuracy_decimals: 0
    icon: mdi:memory

  - platform: template
    name: "Minimum Free Heap"
    id: min_free_heap_sensor
    unit_of_measurement: "B"
    state_class: measurement
    accuracy_decimals: 0
    icon: mdi:memory

  - platform: template
    name: "Heap Usage"
    id: heap_fragmentation_sensor
    unit_of_measurement: "%"
    state_class: measurement
    accuracy_decimals: 1
    icon: mdi:memory
```

### Step 2: Reference them in geappliances_bridge

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  adapter_address: 0xE5 
  appliance_api_parsing: false
  
  # NEW: Add heap monitoring sensors
  free_heap_sensor: free_heap_sensor
  min_free_heap_sensor: min_free_heap_sensor
  heap_fragmentation_sensor: heap_fragmentation_sensor
  
  custom_erds:
    - 0xF016
    # ... rest of your ERDs ...
```

## What to Monitor

### Free Heap Sensor
- **Healthy**: > 50,000 bytes (50 KB)
- **Warning**: 30,000 - 50,000 bytes
- **Critical**: < 30,000 bytes (crash likely)

### Minimum Free Heap Sensor
- Watch for downward trend over time (indicates memory leak)
- Should stabilize after initial bridge setup

### Heap Fragmentation Sensor
- **Healthy**: < 30%
- **Warning**: 30-60%
- **Critical**: > 60% (allocation failures possible)

## Expected Values for ESP32-C6

Your ESP32-C6 has approximately 320 KB of total SRAM:
- At boot: ~250-280 KB free
- After WiFi+MQTT: ~180-200 KB free
- After bridge initialization: ~100-150 KB free
- During heavy polling: may dip to 80-100 KB

If you see values consistently below 60 KB or fragmentation above 50%, that's likely causing your FreeRTOS idle task crashes.

## Next Steps

1. Update your `gea-esphome-zonelinec6.yaml` with the sensor definitions above
2. Compile and deploy
3. Monitor the sensors in Home Assistant for 24-48 hours
4. Share the sensor readings with me - I can help analyze if memory is the root cause

The data will definitively tell us if memory exhaustion is causing the crashes you're seeing in the logs.
