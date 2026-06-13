# Polling Bridge Behavior Specification

This document defines the requirements for the GE Appliances polling bridge, covering ERD discovery, steady-state polling, and Home Assistant discovery timing.

## 1. Polling ERD Discovery

### Requirement 1.1: Definitive Response Per ERD

Each ERD read during discovery MUST receive a definitive response from the GEA client before the next ERD read is issued. The discovery logic MUST NOT use timers to advance to the next ERD; all retry and timeout behavior is handled by the lower-level GEA2/GEA3 client.

### Requirement 1.2: Successful Read

If an ERD read completes successfully with data:
- The ERD is added to the polling list.
- The ERD data is published to MQTT.
- The next ERD in the discovery list is read.

### Requirement 1.3: Not Supported Response

If an ERD read returns a "not supported" response:
- The ERD is NOT added to the polling list.
- The ERD is permanently excluded from future polling (added to the exclusion set).
- The next ERD in the discovery list is read.

### Requirement 1.4: Timeout / No Response

If an ERD read times out (all GEA client retries exhausted):
- The ERD is NOT added to the polling list.
- The ERD is permanently excluded from future polling (added to the exclusion set).
- The next ERD in the discovery list is read.

### Requirement 1.5: One ERD at a Time

During discovery, only ONE ERD read shall be outstanding in the GEA client read buffer at any time. The next ERD read is issued only after the previous read has received a definitive response (success, not supported, or timeout).

### Implementation

The discovery states (`state_add_common_erds`, `state_add_energy_erds`, `state_add_appliance_api_feature_erds`, `state_probe_api_parsed_erds`, `state_add_appliance_erds`) each:
- Send one read request on entry.
- Delegate signal handling to `handle_discovery_list_signals`, which processes `signal_read_completed` and `signal_read_failed` only.
- Call `send_next_read_request` after each response, which increments `erd_index` and sends the next read.
- Transition to the next discovery state when all ERDs in the current list are processed.

### Prohibited

- Using `signal_timer_expired` to advance discovery ERD index.
- Arming retry timers in discovery states.
- Sending multiple ERD reads concurrently during discovery.
- Adding ERDs to the polling list without a definitive response.

### Verification

Tests must verify that discovery progresses only through `signal_read_completed` and `signal_read_failed` callbacks, never through timer expiration.

---

## 2. Steady-State Polling

### Requirement 2.1: All ERDs Read at Cycle Start

At the start of each polling cycle (triggered by the polling timer), all valid ERDs in the polling list MUST be added to the GEA client read buffer simultaneously. There is no sequential or one-at-a-time behavior in steady-state polling.

### Requirement 2.2: Cycle Completion Gate

The next polling cycle MUST NOT begin until BOTH conditions are met:
1. All ERDs in the polling list have received a response (successful read or failure).
2. The polling period timer has expired since the previous cycle started.

A failed read (timeout, not supported, or queue full) counts as a "completed" response for cycle-tracking purposes.

### Requirement 2.3: Failed Reads Must Not Block Cycle Completion

Failed ERD reads during steady-state polling MUST NOT block the polling cycle from completing. When any ERD read fails, the cycle completion counter MUST be incremented so the cycle can finish once all ERDs have responded (success or failure). The polling loop MUST never stall indefinitely waiting for a response that will never arrive.

### Requirement 2.4: Restart Pending

If the polling timer fires while a cycle is still in progress (reads outstanding but not all responses received), the cycle continues to completion. The next cycle starts immediately after the last ERD responds, without waiting for another timer expiration.

### Implementation

- `signal_polling_timer_expired` sends all reads via `send_poll_read_requests_bounded`.
- `cycle_completed_count` increments on each `signal_read_completed` and `signal_read_failed`.
- When `cycle_completed_count >= polling_list_count`, the cycle is complete.
- If `restart_pending` is true (timer fired mid-cycle), the next cycle starts immediately.
- If `polling_timer_armed` is true (timer hasn't fired yet), the code waits for the timer.
- If `polling_timer_armed` is false and `restart_pending` is false, the next cycle starts immediately.

### Prohibited

- Starting a new polling cycle before all ERDs have responded.
- Starting a new polling cycle before the polling timer has expired (unless `restart_pending` is set).
- Using retry timers for individual ERD reads in steady-state polling.
- Allowing failed reads to stall the cycle indefinitely.

### Verification

Tests must verify that:
- All ERDs are read simultaneously at cycle start and that cycle completion requires both full response coverage and timer expiration.
- Failed reads (timeout, not supported) increment the cycle completion counter and allow the cycle to finish.
- Cycles with mixed success and failure outcomes complete normally and subsequent cycles begin when the polling timer fires.
- Cycles where all reads fail still complete and do not require the appliance_lost_timeout to recover.

---

## 3. Home Assistant Discovery Timing

### Requirement 3.1: Poll Mode

In poll mode, HA discovery MUST NOT initiate until the polling bridge has completed ERD discovery (all discovery lists processed, `polling_list_complete` is true).

### Requirement 3.2: Subscription Mode

In subscription mode, HA discovery MUST NOT initiate until the subscription has been idle for a quiet window period (default 10 seconds) with no new ERD registrations. A safety cap (default 30 seconds) ensures discovery is never permanently blocked.

### Requirement 3.3: Auto Mode

In auto mode, HA discovery MUST NOT initiate until BOTH conditions are met:
1. The subscription quiet window has elapsed (same as subscription mode).
2. If the polling bridge is active (e.g., for custom ERDs), the polling discovery list has been completed.

### Implementation

The `HaDiscoveryManager::run()` method checks readiness based on the bridge mode:
- Poll mode: `ready = polling_list_complete`
- Subscription mode: `ready = quiet_window_elapsed` (with safety cap)
- Auto mode: `ready = quiet_window_elapsed AND (polling_list_complete if polling bridge exists)`

The `on_discovery_complete` callback of the polling bridge updates the HA discovery registry with the live ERD snapshot and signals the startup HSM via `signal_bridge_ready`.

### Prohibited

- Initiating HA discovery before ERD registration has settled.
- In auto mode, initiating HA discovery before both subscription quiet window and polling discovery are complete.

### Verification

Tests must verify that HA discovery does not start prematurely in any mode, and that in auto mode, both subscription and polling conditions are satisfied.

---

## 4. Startup HSM Gating

### Requirement 4.1: Wait for Discovery Completion

The startup HSM MUST NOT transition to steady-state operation (`startup_state_subscription_watch`) until the polling bridge has completed ERD discovery and sent `signal_bridge_ready`.

### Requirement 4.2: Bridge Ready Signal

The polling bridge sends `signal_bridge_ready` to the startup HSM via the `on_discovery_complete` callback when entering `state_polling`. This signal carries the completed ERD registry snapshot for HA discovery.

### Implementation

- `startup_state_bridge_init` does NOT auto-transition on `signal_mqtt_connected`.
- It waits for `signal_bridge_ready` before transitioning to `startup_state_subscription_watch`.
- The `on_discovery_complete` callback updates `ha_discovery_manager_.set_registered_erds()` and sends the signal.

### Verification

Tests must verify that the startup HSM remains in `startup_state_bridge_init` until `signal_bridge_ready` is received.

---

## 5. Custom ERD Handling

### Requirement 5.1: Subscription Mode — Separate Polling Bridge After Registration Settles

When custom ERDs are defined in subscription or auto mode (with subscription active), the custom ERD polling phase MUST NOT start until the subscription registration phase has settled. Settlement is defined as a quiet window period (default 10 seconds) with no new ERDs seen via subscription publications.

Custom ERDs are polled by a **separate** polling bridge instance running alongside the subscription bridge. The subscription bridge continues to handle all standard ERD publications; the polling bridge handles only the custom ERDs that may not be covered by subscription.

### Requirement 5.2: Poll Mode — Discovered Alongside Standard ERDs

When custom ERDs are defined in poll mode (or auto mode that falls back to poll), custom ERDs MUST be appended to the end of the polling discovery list and go through the same discovery/probe phase as standard ERDs. A single polling instance handles both standard and custom ERDs.

Custom ERDs that respond successfully during discovery are added to the polling list and registered. Custom ERDs that do not respond (not supported or timeout) are excluded from the polling list, just like any other ERD.

### Requirement 5.3: Auto Mode Fallback

When auto mode falls back from subscription to poll mode, custom ERDs are handled per Requirement 5.2 (discovered alongside standard ERDs).

### Implementation

**Subscription mode:**
- `maybe_start_custom_erd_polling_()` gates on three conditions: in subscription mode, subscription activity confirmed, and `custom_erd_subscription_last_activity_` older than `HA_DISCOVERY_QUIET_MS` (10s).
- `start_custom_erd_polling_()` initializes a separate polling bridge via `mqtt_bridge_polling_init_at_address()` with the custom ERDs as the `api_parsed_list`, which goes through the probe phase.
- The subscription bridge is NOT destroyed; both bridges share the same ERD client.

**Poll mode:**
- `configure_polling_optional_lists_()` sets `mqtt_bridge_polling_.custom_erd_list` and `custom_erd_list_count` before any events fire.
- `state_add_appliance_erds` transitions to `state_add_custom_erds` (instead of `state_polling`) when custom ERDs are configured.
- `state_add_custom_erds` discovers each custom ERD through `handle_discovery_list_signals`, adding successful ones to the polling list and excluding failures.

### Prohibited

- Starting custom ERD polling before the subscription quiet window has elapsed in subscription mode.
- Using multiple polling bridge instances in poll mode.
- Adding custom ERDs to the polling list without going through the discovery/probe phase.
- Starting custom ERD polling when no custom ERDs are defined.

### Verification

Tests must verify that custom ERD polling in subscription mode does not start until the quiet window elapses, and that in poll mode, custom ERDs go through the same discovery phase as standard ERDs — successful ones are polled, failed ones are excluded.
