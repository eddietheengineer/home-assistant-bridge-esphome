# Heap Monitoring Sensors - Usage Guide

## What Was Added

I've added three heap monitoring sensors to the `geappliances_bridge` component:

1. **Free Heap** - Current available heap memory in bytes
2. **Minimum Free Heap** - Lowest free heap reached since boot (in bytes)  
3. **Heap Fragmentation** - Percentage of memory used (0-100%, lower is better)

These sensors update every **60 seconds** and will help you diagnose the memory issues causing your crashes.

## How to Use in Your YAML Configuration

Add these sensors to your YAML file, then reference them in the `geappliances_bridge` component:

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

Then update your `geappliances_bridge` configuration to use these sensors:

```yaml
geappliances_bridge:
  gea3_uart_id: gea3_uart
  adapter_address: 0xE5 
  appliance_api_parsing: false
  
  # NEW: Add heap monitoring sensors
  free_heap_sensor: free_heap_sensor
  min_free_heap_sensor: min_free_heap_sensor
  heap_fragmentation_sensor: heap_fragmentation_sensor
  
  # ...existing custom_erds...
  custom_erds:
    - 0xF016
    - 0xF029
    # ... rest of your ERDs ...
```

## What to Look For

Once deployed, monitor these sensors in Home Assistant:

### **Free Heap**
- **Healthy**: > 50,000 bytes (50 KB)
- **Warning**: 30,000 - 50,000 bytes
- **Critical**: < 30,000 bytes (likely to crash)

### **Minimum Free Heap**
- This shows the lowest point reached - if this keeps decreasing over time, you have a memory leak

### **Heap Usage**
- **Healthy**: < 70% (meaning > 30% free)
- **Warning**: 70-85%
- **Critical**: > 85% (can cause allocation failures even with free memory available)

## Expected Behavior for Your Setup

With your Zoneline appliance (139 ERDs total), you should see:
- Initial free heap: ~150-200 KB (ESP32-C6 has ~320 KB total)
- After bridge initialization: ~80-120 KB
- During heavy polling: may dip to 60-80 KB

If you see free heap dropping below 50 KB or usage above 80%, that correlates with the crashes you're experiencing.

## Creating Alerts in Home Assistant

You can create automations to alert you when memory gets low:

```yaml
automation:
  - alias: "Low Memory Warning"
    trigger:
      - platform: numeric_state
        entity_id: sensor.free_heap
        below: 50000
        for: "2 minutes"
    action:
      - service: persistent_notification.create
        data:
          title: "Memory Warning"
          message: "GE Bridge free heap is critically low - crash may occur"
```

## Next Steps

1. Add the sensor definitions to your YAML
2. Reference them in `geappliances_bridge`
3. Deploy and monitor for 24-48 hours
4. Share the sensor data back here - I can help analyze if the memory usage is the root cause of your crashes

The sensor data will tell us definitively if memory exhaustion is causing the FreeRTOS idle task crashes you're seeing.
