# Discovery Refresh Button — Specification

## 1. Overview

### 1.1 Purpose

The Discovery Refresh Button is an ESPHome `button::Button` component that triggers a Home Assistant discovery cleanup and device restart when pressed. It provides a user-accessible mechanism to force the bridge to clear all retained discovery messages from the MQTT broker and reboot, causing Home Assistant to rediscover the device from scratch on the next boot.

### 1.2 Responsibilities

- Expose a single press action via the ESPHome Button component interface
- Guard against null bridge pointer before delegating
- Delegate the actual cleanup logic to `GeappliancesBridge::trigger_discovery_refresh()`

### 1.3 Not Responsible For

- Discovery cleanup execution (delegated to `GeappliancesBridge::trigger_discovery_refresh()`)
- MQTT message publishing (delegated to `ha_discovery_cleanup` module)
- Device reboot scheduling (delegated to `GeappliancesBridge` loop)
- Button lifecycle management (owned by ESPHome's component framework)

---

## 2. Class Hierarchy

```
esphome::button::Button          (abstract base — provides press_action() interface)
    └── esphome::geappliances_bridge::DiscoveryRefreshButton
```

The class extends `button::Button` and implements the pure virtual `press_action()` method. The base class provides:

- Virtual destructor
- `press_action()` — pure virtual, called when the button is pressed
- `add_on_press_callback()` — registers callbacks invoked on press
- `press()` — test helper that fires all registered callbacks

---

## 3. Public API

### 3.1 Constructor

```cpp
DiscoveryRefreshButton(GeappliancesBridge* bridge) : bridge_(bridge) {}
```

| Parameter | Description |
|-----------|-------------|
| `bridge` | Raw pointer to the owning `GeappliancesBridge` instance. May be `nullptr` (guarded against at press time). |

The constructor stores the bridge pointer for later use during `press_action()`. No initialization, validation, or side effects are performed.

### 3.2 press_action()

```cpp
void press_action() override {
  if (bridge_ != nullptr) {
    bridge_->trigger_discovery_refresh();
  }
}
```

Called by ESPHome's button framework when the button is pressed. Delegates to `GeappliancesBridge::trigger_discovery_refresh()` after a null guard on `bridge_`.

---

## 4. Integration with GeappliancesBridge

### 4.1 Bridge Pointer

The button holds a raw pointer (`GeappliancesBridge* bridge_`) to the bridge instance. The pointer is:

- Set once at construction time
- Never modified after construction
- Not reference-counted or owned by the button

The bridge is expected to outlive the button in normal operation, as both are managed by ESPHome's component lifecycle.

### 4.2 trigger_discovery_refresh() Flow

When `press_action()` delegates to `bridge_->trigger_discovery_refresh()`, the bridge performs the following:

1. **Idempotency guard**: If `discovery_refresh_in_progress_` is `true`, log a warning and return immediately. Prevents concurrent cleanup sessions.
2. **Steady-state guard**: If `steady_state_reached_` is `false`, log a warning and return. Discovery refresh is only valid after the bridge has completed its initial startup and reached steady-state operation.
3. **Processing guard**: If `ha_discovery_manager_is_processing()` returns `true` (manager is in `building` or `discovering` state), log a warning and return. Cleanup must not start while the discovery manager is actively processing.
4. **Configure cleanup**: On ESP-IDF platforms, call `ha_discovery_cleanup_configure()` with the device ID, MQTT client interface, and time source, then `ha_discovery_cleanup_start()`.
5. **Set in-progress flag**: Set `discovery_refresh_in_progress_` to `true`.

On non-ESP-IDF platforms, steps 4-5 are no-ops (dependency pointers are cast away with `(void)` suppressions).

### 4.3 Cleanup Completion and Reboot

The bridge's main loop polls the cleanup module while `discovery_refresh_in_progress_` is `true`:

1. Call `ha_discovery_cleanup_run()` each loop iteration
2. When `ha_discovery_cleanup_is_done()` returns `true`:
   - Set `discovery_refresh_in_progress_` to `false`
   - Log info: "HA discovery cleanup complete, restarting device..."
   - Delay 500 ms via `vTaskDelay` to allow final retained-clear messages to transmit
   - Call `esphome::App.reboot()` to restart the device

---

## 5. State Transitions

### 5.1 Press When Bridge Is Idle (Steady State, No Processing)

| Condition | Result |
|-----------|--------|
| `bridge_ != nullptr` | Delegates to `trigger_discovery_refresh()` |
| `steady_state_reached_ == true` | Passes steady-state guard |
| `discovery_refresh_in_progress_ == false` | Passes idempotency guard |
| `ha_discovery_manager_is_processing() == false` | Passes processing guard |
| **Outcome** | Cleanup configured, started, and `discovery_refresh_in_progress_` set to `true`. The main loop will drive cleanup to completion and reboot the device. |

### 5.2 Press While Refresh Already In Progress

| Condition | Result |
|-----------|--------|
| `discovery_refresh_in_progress_ == true` | **Blocked** — warning logged: "Discovery refresh already in progress, ignoring". No state change. |

### 5.3 Press Before Steady State Reached

| Condition | Result |
|-----------|--------|
| `steady_state_reached_ == false` | **Blocked** — warning logged: "Cannot refresh discovery: appliance bridge not in steady state". No state change. |

### 5.4 Press While Discovery Manager Is Processing

| Condition | Result |
|-----------|--------|
| `ha_discovery_manager_is_processing() == true` (state is `building` or `discovering`) | **Blocked** — warning logged: "Cannot refresh discovery: manager still processing". No state change. |

### 5.5 Press With Null Bridge

| Condition | Result |
|-----------|--------|
| `bridge_ == nullptr` | **Silent no-op** — `press_action()` returns without logging or error. |

---

## 6. Safety

### 6.1 Null Bridge Guard

`press_action()` checks `bridge_ != nullptr` before calling `trigger_discovery_refresh()`. This protects against:

- Early construction before the bridge is available
- Partial initialization scenarios
- Test environments where the bridge may not be instantiated

The guard is silent — no log message is emitted when `bridge_` is null.

### 6.2 Idempotency

`trigger_discovery_refresh()` guards against concurrent execution via `discovery_refresh_in_progress_`. Multiple rapid presses of the button are safely ignored after the first press initiates cleanup. The flag is cleared only after cleanup completes and before the device reboots.

### 6.3 Steady-State Requirement

The button refuses to trigger cleanup until `steady_state_reached_` is `true`. This flag is set once during the bridge's startup sequence and never cleared. It ensures the appliance bridge has completed its initial connection and data polling before allowing a disruptive cleanup operation.

### 6.4 Processing Guard

The button refuses to start cleanup while the discovery manager is in an active processing state (`building` or `discovering`). This prevents interference with ongoing discovery operations and avoids corrupting the discovery state.

---

## 7. Dependencies

| Dependency | Role |
|------------|------|
| `esphome/components/button/button.h` | Base class `button::Button` with `press_action()` interface |
| `geappliances_bridge.h` | `GeappliancesBridge` class with `trigger_discovery_refresh()` method |
| `esphome/core/log.h` | Logging (`ESP_LOGW`, `ESP_LOGI`) via the bridge's `trigger_discovery_refresh()` |
| `ha_discovery_cleanup.h` | Cleanup module used by the bridge to clear retained discovery messages |

---

## 8. Invariants

1. **Single bridge reference:** `bridge_` is set once at construction and never modified.
2. **No ownership:** The button does not own or manage the lifecycle of the bridge.
3. **Delegation-only behavior:** The button performs no cleanup logic itself — all work is delegated to `GeappliancesBridge::trigger_discovery_refresh()`.
4. **Guard chain is ordered:** `trigger_discovery_refresh()` checks guards in a fixed order: idempotency, steady-state, then processing. The order matters because the idempotency guard is the cheapest check and prevents re-entry during an active cleanup.
5. **Non-ESP-IDF is a no-op:** On non-ESP-IDF platforms, `trigger_discovery_refresh()` performs the guard checks but does not configure or start the cleanup module.

---

## 9. Known Limitations

1. **No visual feedback:** The button provides no indication of whether the press was accepted or rejected. The user must check logs to determine the outcome.
2. **Device reboot is mandatory:** After cleanup completes, the device always reboots. There is no option to complete cleanup without rebooting.
3. **500 ms pre-reboot delay is fixed:** The delay between cleanup completion and reboot is hardcoded to 500 ms. If the MQTT broker is slow to acknowledge retained-clear messages, some messages may be lost.
4. **Non-ESP-IDF platforms have no effect:** The cleanup and reboot flow is only implemented for ESP-IDF. On other platforms, pressing the button performs guard checks but takes no action.
5. **No cancellation:** Once cleanup has started, there is no way to cancel it. The device will proceed through cleanup and reboot.
