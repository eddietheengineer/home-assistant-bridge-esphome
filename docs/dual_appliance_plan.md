# Dual Appliance Support Plan

**Goal:** Support two appliances on a single GEA3 bus (e.g., addresses 0xC0 and 0xC1), each with its own device ID, ERD cache, MQTT topics, and startup lifecycle — sharing one UART.

**Motivation:** Some installations have two GE appliances (e.g., refrigerator and range) on the same GEA3 serial bus, each at a different host address. A single bridge can only talk to one appliance. Two bridge instances on one UART would expose both to Home Assistant.

---

## Current Architecture (Single Bridge)

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

---

## What Must Change

### Phase 1: HSM Services Registry (Enabler)

**Problem:** `geappliances_bridge_startup_hsm.cpp` uses a file-scope `static IBridgeServices* g_bridge_services` — a singleton. Two bridges would overwrite each other.

**Fix:** Replace the global with a small registry keyed by `tiny_hsm_t*`:

```cpp
struct hsm_service_entry_t {
  tiny_hsm_t* hsm;
  IBridgeServices* services;
};
static hsm_service_entry_t s_hsm_registry[4];  // 4 is generous for 2 bridges
```

- `services_from_hsm(tiny_hsm_t* hsm)` → linear search by HSM pointer
- `set_bridge_services(tiny_hsm_t* hsm, IBridgeServices* services)` → register or update by HSM pointer; reuse cleared slots

**Files:** `geappliances_bridge_startup_hsm.cpp`, `geappliances_bridge_startup_hsm.h`, `geappliances_bridge.cpp` (caller), `startup_hsm_test.cpp` (tests)

**Risk:** Low. The HSM state functions already receive `tiny_hsm_t* hsm` as their first parameter — the pointer is available for lookup. No library changes needed.

---

### Phase 2: Shared UART Transport

**Problem:** Each `GeappliancesBridge` owns its own `esphome_uart_adapter_t` and `tiny_gea3_interface_t`. Two bridges on the same UART would each try to read/write the same hardware, causing:
- Duplicate reads (both adapters pull bytes from the ESPHome UART)
- Colliding writes (both interfaces queue sends to the same UART)
- Timer conflicts (both adapters register poll timers in the shared `timer_group_`)

**Approach A — Single shared UART adapter, multiple GEA3 interfaces (Recommended)**

Share the `esphome_uart_adapter_t` between bridges. Each bridge gets its own `tiny_gea3_interface_t` bound to the shared UART adapter. The UART adapter's receive event is broadcast to all registered interfaces.

Changes:
- `esphome_uart_adapter_t` gains a subscription list: `tiny_event_t* interface_receive_subscribers[N]`
- When the adapter reads a byte, it publishes the receive event — each interface's subscription handler processes it
- Each interface maintains its own send queue (already the case)
- The UART adapter's `send_complete` event routes to the correct interface (match by send buffer)

**Approach B — Single bridge, multiple appliances (Simpler but less flexible)**

Keep one `GeappliancesBridge` instance, but add a secondary appliance slot. The bridge maintains two sets of state (ERD cache, bridges, etc.) and polls both host addresses. This avoids the shared UART problem but doubles the complexity inside a single component.

**Recommendation:** Approach A. It's the cleaner separation — each appliance is a first-class bridge instance. The shared UART adapter is a natural abstraction (one physical UART, multiple protocol stacks).

---

### Phase 3: Per-Bridge ERD Client and MQTT Adapter

**ERD Client:** Each bridge already owns its own `tiny_gea3_erd_client_t`. No change needed — the ERD client binds to a `tiny_gea3_interface_t`, which in Phase 2 binds to the shared UART adapter.

**MQTT Client Adapter:** Each bridge owns its own `esphome_mqtt_client_adapter_t`. This is already per-bridge — no change needed. The adapter uses the ESPHome MQTT client (singleton) internally, which is fine — it's a shared resource like the UART.

---

### Phase 4: Shared Autodiscovery

**Problem:** Autodiscovery broadcasts to 0xFF and waits for any response. With two appliances, both respond. The current `AutodiscoveryManager` finds the first responder and stops.

**Options:**
- **A.** Each bridge runs its own autodiscovery, filtering responses by expected address range. The first bridge discovers 0xC0, the second discovers 0xC1.
- **B.** A shared autodiscovery that discovers all appliances and distributes them to bridges.

**Recommendation:** Option A. Each bridge configures its expected host address in YAML (or runs discovery independently). The autodFF broadcast reaches both appliances; each bridge's ERD client receives the response and routes it to the correct bridge based on the source address in the GEA3 frame.

This requires the `tiny_gea3_interface_t` to include the source address in its receive event, and each bridge's ERD client to filter by its expected address. The GEA3 protocol already includes source/destination addresses in every frame — this information is available.

---

### Phase 5: ESPHome YAML Configuration

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
    # Optionally pre-set the expected host address to skip autodiscovery
    # or run autodiscovery and filter by response address

  - gea3_uart_id: gea_bus
    device_id: "range"
    client_address: 0xE5
```

The `__init__.py` `to_code()` function already handles multiple component instances — ESPHome's `cv.Schema` supports lists. The `validate_at_least_one_uart` check would need to apply per-instance, not globally.

---

## What Does NOT Need to Change

- **ERD cache** — already per-bridge
- **MQTT publisher** — already per-bridge
- **Poll/subscribe bridges** — already per-bridge
- **Write bridge** — already per-bridge
- **Feature bit manager** — already per-bridge
- **Device identity manager** — already per-bridge
- **`esphome_mqtt_client_adapter_t`** — already per-bridge (wraps the shared ESPHome MQTT singleton)

---

## Implementation Order

1. **HSM services registry** (Phase 1) — unblocks multiple HSM instances
2. **Shared UART adapter** (Phase 2) — unblocks multiple bridges on one UART
3. **Address-filtered autodiscovery** (Phase 4) — enables each bridge to find its appliance
4. **YAML multi-instance support** (Phase 5) — user-facing configuration
5. **Testing** — dual-bridge simulation tests

## Risks and Tradeoffs

| Risk | Mitigation |
|------|-----------|
| UART receive event fan-out adds latency | Each interface processes frames independently; no serialization needed |
| Send collisions on shared UART | Each interface has its own send queue; the UART adapter serializes sends |
| Timer group conflicts | Both adapters register in the same `timer_group_`; `tiny_timer_group_run()` services all timers |
| Memory: 2× bridge state | ~50KB per bridge (buffers dominate); ESP32 has 520KB free RAM typical |
| Autodiscovery ambiguity | Pre-configure host addresses in YAML to skip autodFF broadcast |

## Out of Scope

- GEA2 dual-appliance support (GEA2 is single-appliance by protocol design)
- More than 2 appliances on one bus (the registry size of 4 handles this, but testing and YAML UX are not planned)
- Load balancing or failover between bridges
