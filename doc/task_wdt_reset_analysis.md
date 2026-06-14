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
- Autodiscovery → Feature bits → Bridge init → HA discovery → Steady-state polling
- Lived for **153 seconds** before crashing during normal polling operation
- This crash had `aioesphomeapi` disconnect at 11:24:56, then WDT at 11:25:15 (~19s later)

### 6. Safe Mode Breaks the Loop

At boot attempt 10, safe mode activates and skips custom component setup. The device connects to WiFi and remains stable. This confirms the crash is in the bridge component code path, not in core ESPHome or WiFi/MQTT infrastructure.

## Root Cause Hypothesis

The `loopTask` watchdog (default 3s on ESP32-C6) is being starved because the main event loop is blocked by synchronous operations during startup. The most likely culprits:

1. **Blocking MQTT operations** — The consistent `mqtt took a long time` warnings (~104ms per operation) suggest the MQTT client is doing synchronous network I/O on the main loop. Accumulated across multiple subscribe/publish calls during startup, this can exceed the 3s watchdog window.

2. **Blocking UART operations** — The GEA3 UART adapter performs synchronous reads during autodiscovery and feature bit enumeration. If the appliance board is slow to respond or unresponsive, the UART read can block indefinitely.

3. **Combined effect** — MQTT + UART operations happening in the same loop iteration can push total blocking time past the watchdog threshold.

## Recommendations

1. **Increase the task watchdog timeout** — Add `esphome: on_boot: priority: -100` with `esp_task_wdt_config` to give more headroom during startup, or configure the watchdog timeout in the platformio/ESPhome config.

2. **Make UART reads non-blocking** — Ensure all GEA3 UART operations use timeouts and yield control back to the event loop between reads.

3. **Yield in MQTT-heavy startup paths** — Break up batches of MQTT subscribe/publish calls with `yield()` or `delay(0)` calls to feed the watchdog.

4. **Add `App.feed_wdt()` at strategic points** — In long-running startup sequences (feature bit enumeration, ERD registration), explicitly feed the watchdog between operations.

5. **Investigate the steady-state crash** — The one run that reached polling mode also crashed (153s uptime), suggesting the blocking issue persists in the polling loop, not just during startup.
