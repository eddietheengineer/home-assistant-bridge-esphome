# Roadmap

Future features and architectural improvements.

## Dual Appliance Support

**Goal:** Support two appliances on a single GEA3 bus (e.g., addresses 0xC0 and 0xC1), each with its own device ID, ERD cache, MQTT topics, and startup lifecycle — sharing one UART.

**Motivation:** Some installations have two GE appliances (e.g., refrigerator and range) on the same GEA3 serial bus, each at a different host address. A single bridge can only talk to one appliance.

### Current Architecture (Single Bridge)

```
ESPHome Component (GeappliancesBridge)
├── 1× UART (GEA3)
├── 1× esphome_uart_adapter_t
├── 1× tiny_gea3_interface_t
├── 1× tiny_gea3_erd_client_t
├── 1× AutodiscoveryManager
├── 1× DeviceIdentityManager
├── 1× FeatureBitManager
├── 1× erd_cache_t
├── 1× erd_cache_mqtt_publisher_t
├── 1× esphome_mqtt_client_adapter_t
├── 1× erd_bridge_subscribe_t or erd_bridge_poll_t
├── 1× erd_write_bridge_t
├── 1× startup_hsm_t
└── 1× IBridgeServices (the bridge itself)
```

Each bridge owns everything from UART adapter to MQTT publisher. The startup HSM uses a **global** `g_bridge_services` pointer to route back to the bridge — this is the single point of failure for multi-instance support.

### Implementation Plan

#### Phase 1: HSM Services Registry (Enabler)

**Problem:** `geappliances_bridge_startup_hsm.cpp` uses a file-scope `static IBridgeServices* g_bridge_services` — a singleton. Two bridges would overwrite each other.

**Fix:** Replace the global with a small registry keyed by `tiny_hsm_t*`:

```cpp
struct hsm_service_entry_t {
  tiny_hsm_t* hsm;
  IBridgeServices* services;
};
static hsm_service_entry_t s_hsm_registry[4];
```

- `services_from_hsm(tiny_hsm_t* hsm)` → linear search by HSM pointer
- `set_bridge_services(tiny_hsm_t* hsm, IBridgeServices* services)` → register or update

**Risk:** Low. The HSM state functions already receive `tiny_hsm_t* hsm` as their first parameter.

#### Phase 2: Shared UART Transport

**Problem:** Each `GeappliancesBridge` owns its own `esphome_uart_adapter_t`. Two bridges on the same UART would each try to read/write the same hardware.

**Approach A — Single shared UART adapter, multiple GEA3 interfaces (Recommended)**

Share the `esphome_uart_adapter_t` between bridges. Each bridge gets its own `tiny_gea3_interface_t` bound to the shared UART adapter. The UART adapter's receive event is broadcast to all registered interfaces.

**Approach B — Single bridge, multiple appliances**

Keep one `GeappliancesBridge` instance, but add a secondary appliance slot. Simpler but less flexible.

#### Phase 3: Per-Bridge ERD Client and MQTT Adapter

Each bridge already owns its own `tiny_gea3_erd_client_t` and `esphome_mqtt_client_adapter_t`. No change needed.

#### Phase 4: Shared Autodiscovery

**Problem:** Autodiscovery broadcasts to 0xFF and waits for any response. With two appliances, both respond.

**Recommendation:** Each bridge runs its own autodiscovery, filtering responses by expected address range. The GEA3 protocol already includes source/destination addresses in every frame.

#### Phase 5: ESPHome YAML Configuration

```yaml
uart:
  - id: gea_bus
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 230400

geappliances_bridge:
  - gea3_uart_id: gea_bus
    device_id: "fridge"
    client_address: 0xE4

  - gea3_uart_id: gea_bus
    device_id: "range"
    client_address: 0xE5
```

### What Does NOT Need to Change

- ERD cache, MQTT publisher, poll/subscribe bridges, write bridge, feature bit manager, device identity manager — all already per-bridge.

### Risks and Tradeoffs

| Risk | Mitigation |
|---|---|
| UART receive event fan-out adds latency | Each interface processes frames independently |
| Send collisions on shared UART | Each interface has its own send queue; UART adapter serializes |
| Timer group conflicts | `tiny_timer_group_run()` services all timers |
| Memory: 2× bridge state | ~50KB per bridge; ESP32 has 520KB free RAM typical |
| Autodiscovery ambiguity | Pre-configure host addresses in YAML |

### Out of Scope

- GEA2 dual-appliance support (GEA2 is single-appliance by protocol design)
- More than 2 appliances on one bus
- Load balancing or failover between bridges