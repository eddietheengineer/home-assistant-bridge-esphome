# Task Watchdog Reset Analysis

## Overview

Log file captured on 2026-06-14 from `gea-esphome-haieroduc6` (ESP32-C6 rev0.2) running ESPHome 2026.5.3 / ESP-IDF 5.5.4. Device is a split duct-free AC (appliance type 14, model 1U4248LP2HDA1).

## Summary

The device enters a continuous boot loop caused by **task watchdog timeouts on `loopTask` (CPU 0)**. The watchdog fires repeatedly during the startup sequence, preventing the bridge from reaching steady-state operation. After 10 consecutive failed boots, ESPHome's safe mode activates, which skips custom component setup and stabilizes.

## Reset Timeline

| # | Wall Clock | Uptime (ms) | Stage at Crash | Boot Attempt |
|---|-----------|-------------|----------------|--------------|
| 1 | 11:22:41 | 26,305 | Bridge init — ERD registration | 0 |
| 2 | 11:25:15 | 153,469 | Steady-state polling (only successful run) | 1 |
| 3 | 11:25:27 | 12,001 | MQTT connected, pre-autodiscovery | 0 |
| 4 | 11:25:47 | 20,662 | Bridge init — ERD registration | 1 |
| 5 | 11:25:58 | 11,309 | MQTT connected, pre-autodiscovery | 2 |
| 6 | 11:26:15 | 16,616 | Feature bits phase | 3 |
| 7 | 11:26:26 | 11,373 | MQTT connected, pre-autodiscovery | 4 |
| 8 | 11:26:37 | 10,807 | MQTT connected, pre-autodiscovery | 5 |
| 9 | 11:26:48 | 10,979 | MQTT connected, pre-autodiscovery | 6 |
| 10 | 11:26:58 | 10,768 | MQTT connected, pre-autodiscovery | 7 |
| 11 | 11:27:09 | 10,909 | Autodiscovery phase | 8 |
| 12 | 11:27:20 | 10,773 | MQTT connected, pre-autodiscovery | 9 |
| — | 11:27:21 | — | **Safe mode activated** | 10 |

## Patterns

### 1. Crash Always at `loopTask` (CPU 0)

Every crash has the identical signature:
```
task_wdt: Task watchdog got triggered.
  - loopTask (CPU 0)
Tasks currently running:
  CPU 0: IDLE
```
The running task is `IDLE`, meaning `loopTask` stopped feeding the watchdog — the main event loop is blocked in a synchronous operation.

### 2. Identical Register Dumps

All crashes show the same CPU 0 register dump (MEPC `0x408042fc`, RA `0x408042ea`), indicating the crash occurs at the same code location each time — inside the watchdog handler itself, not at varying call sites.

### 3. MQTT Connection Precedes Every Crash

Every boot that crashes shows `mqtt took a long time for an operation (~104 ms)` immediately after MQTT connects, followed by the watchdog firing within 5–15 seconds. The MQTT library appears to be performing blocking operations on the main loop thread.

### 4. Crash Timing Converges to ~11 seconds

- First crash: 26s (initial boot, more setup work)
- Second crash: 153s (only run to reach steady state)
- Subsequent crashes: converge to **10–12 seconds** after boot

This convergence suggests a fixed timeout or operation that blocks for a deterministic duration.

### 5. Only One Run Reached Steady State

Boot #2 (11:22:42–11:25:15) is the only run that completed the full startup sequence:
- Autodiscovery → Feature bits → Bridge init → Steady-state polling
- Lived for **153 seconds** before crashing during normal polling operation
- This crash had `aioesphomeapi` disconnect at 11:24:56, then WDT at 11:25:15 (~19s later)

### 6. Safe Mode Breaks the Loop

At boot attempt 10, safe mode activates and skips custom component setup. The device connects to WiFi and remains stable. This confirms the crash is in the bridge component code path, not in core ESPHome or WiFi/MQTT infrastructure.

## Root Cause (Confirmed)

The MQTT connection FSM in `loop()` runs **before** the first `esp_task_wdt_reset()` call.
During startup, three blocking operations execute without feeding the watchdog:

1. `subscribe_write_topic()` — acquires the IDF MQTT mutex, sends a SUBSCRIBE packet to the broker
2. `drain_pending_updates()` (FLUSHING state) — publishes up to 5 pending ERD messages synchronously, each acquiring the IDF MQTT mutex (~100 ms per publish)
3. `drain_pending_updates()` (RUNNING state) — same blocking pattern in steady-state

When the pending update queue is large (startup: 50+ ERDs to flush), the FLUSHING state
blocks for multiple loop iterations. Each iteration drains 5 messages at ~100 ms each
(~500 ms per iteration). The TWDT timeout (default 3 s on ESP32-C6) fires before the
first `esp_task_wdt_reset()` at the end of `run_protocol_stack_()`.

The crash timing converges to ~5 s after MQTT connect because: the TWDT is fed once
during the connect transition (DISCONNECTED → SUBSCRIBING), then the SUBSCRIBING and
FLUSHING states block for >3 s before reaching the next feed point.

Boot #2 reached steady state because MQTT connected before feature bit reading started,
so the pending queue was small. It still crashed later (153 s uptime) during polling,
confirming the RUNNING state also needed protection.

## Fix Applied

Added `esp_task_wdt_reset()` calls gated by `#ifdef USE_ESP32` before each blocking
MQTT operation in the SUBSCRIBING, FLUSHING, and RUNNING FSM states
(`geappliances_bridge.cpp`, `loop()` method). This ensures the task watchdog is fed
before the main loop enters any potentially blocking MQTT path.

## Remaining Questions

1. **Steady-state crash** — Boot #2 crashed at 153 s uptime during normal polling.
   The fix adds TWDT feeds in the RUNNING state, but the root cause of that specific
   crash (possibly `aioesphomeapi` disconnect triggering a long reconnection sequence)
   may need separate investigation.

2. **MQTT publish latency** — The consistent ~104 ms per MQTT operation is high.
   Investigating whether the broker, network path, or IDF MQTT client configuration
   can reduce this latency would improve startup time and reduce watchdog pressure.

3. **ERD client read timeouts** — The GEA3 ERD client uses 250 ms timeout × 10 retries
   = 2.5 s per ERD read on failure. During discovery phases, a non-responsive appliance
   could block the protocol stack for extended periods. The GEA2 tight loop already has
   TWDT feeds, but the GEA3 single-pass path does not feed the watchdog between ERD reads.
