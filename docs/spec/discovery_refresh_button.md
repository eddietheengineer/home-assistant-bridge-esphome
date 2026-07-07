# Discovery Refresh Button — Specification

## 1. Overview

### 1.1 Purpose

The Discovery Refresh Button is an ESPHome `button::Button` component that triggers a Home Assistant discovery cleanup, republishes fresh discovery topics, and reboots the device when pressed. It provides a user-accessible mechanism to force the bridge to clear all retained discovery messages from the MQTT broker, publish fresh ones, and reboot — causing Home Assistant to rediscover the device from scratch on the next boot.

### 1.2 Responsibilities

- Expose a single press action via the ESPHome Button component interface
- Guard against null bridge pointer before delegating
- Delegate the actual cleanup logic to `GeappliancesBridge::trigger_discovery_refresh()`

### 1.3 Not Responsible For

- Discovery cleanup execution (delegated to `GeappliancesBridge`)
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

When `press_action()` delegates to `bridge_->trigger_discovery_refresh()`, the bridge delegates to `ota_cleanup_manager_.trigger_discovery_refresh()`. The `OtaCleanupManager` performs:

1. **Idempotency guard:** If `discovery_refresh_in_progress_` (owned by `OtaCleanupManager`) is `true`, log a warning ("Discovery refresh already in progress, ignoring") and return. Prevents concurrent requests.
2. **Queue the request:** Set `discovery_refresh_in_progress_` to `true` and log "Discovery refresh queued, will execute when appliance is ready".

No guard checks for steady state, MQTT readiness, or device ID are performed at press time. The request is queued and will execute in `OtaCleanupManager::loop()` once all prerequisites are met. This allows the user to press the button at any time, even during startup.

### 4.3 Queued Execution in OtaCleanupManager::loop()

The bridge's `loop()` calls `ota_cleanup_manager_.loop()`, which checks `discovery_refresh_in_progress_` and, when the bridge is ready, starts the cleanup:

1. **Wait for readiness:** The manager checks `steady_state_reached_`, `mqtt_client_adapter_initialized_`, and `device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE`.
2. **Start cleanup:** Once ready, `ha_discovery_cleanup_configure()` and `ha_discovery_cleanup_start()` are called, `discovery_refresh_in_progress_` is cleared, and `ota_cleanup_in_progress_` is set to `true`. This hands off to the shared cleanup → publish → reboot path.
3. **Drive cleanup:** `ha_discovery_cleanup_run()` is called each loop iteration. When done, the cleanup module is destroyed and fresh discovery topics are published.
4. **Drive discovery publishing:** `ha_discovery_manager_run()` publishes discovery payloads. When done, `mark_boot_successful_for_reboot()` is called.
5. **Wait then reboot:** After a 5-second delay, `esphome::App.safe_reboot()` performs a graceful reboot.

**Note:** The guard also checks `!ota_cleanup_in_progress_` to prevent interfering with an in-progress OTA cleanup cycle.

---

## 5. State Transitions

### 5.1 Press When Bridge Is Idle (Steady State)

| Condition | Result |
|-----------|--------|
| `bridge_ != nullptr` | Delegates to `trigger_discovery_refresh()`, which forwards to `ota_cleanup_manager_.trigger_discovery_refresh()` |
| `discovery_refresh_in_progress_` (in `OtaCleanupManager`) == false | Passes idempotency guard |
| **Outcome** | `discovery_refresh_in_progress_` set to `true` in `OtaCleanupManager`. The manager's `loop()` will start cleanup immediately (bridge is already ready), then proceed through cleanup → publish → reboot. |

### 5.2 Press While Refresh Already In Progress

| Condition | Result |
|-----------|--------|
| `discovery_refresh_in_progress_` (in `OtaCleanupManager`) == true | **Blocked** — warning logged: "Discovery refresh already in progress, ignoring". No state change. |

### 5.3 Press Before Steady State Reached

| Condition | Result |
|-----------|--------|
| `steady_state_reached_ == false` (or MQTT not ready, or device ID not complete) | **Queued** — `discovery_refresh_in_progress_` set to `true` in `OtaCleanupManager`. The request waits in `OtaCleanupManager::loop()` until all prerequisites are met, then executes. |

### 5.4 Press With Null Bridge

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

`trigger_discovery_refresh()` delegates to `ota_cleanup_manager_.trigger_discovery_refresh()`, which guards against concurrent execution via `discovery_refresh_in_progress_` (owned by `OtaCleanupManager`). Multiple rapid presses of the button are safely ignored after the first press queues the request.

### 6.3 Queued Execution

The request is queued rather than rejected if the bridge is not ready. This eliminates the need for guard checks at press time and allows the user to press the button at any time. The actual cleanup starts in `OtaCleanupManager::loop()` once `steady_state_reached_`, `mqtt_client_adapter_initialized_`, and device ID completion are all true.

### 6.4 Shared Path with OTA

The Discovery Refresh button uses the same cleanup → publish → reboot path as OTA-triggered discovery. Both paths share `ota_cleanup_in_progress_`, `ota_discovery_publishing_`, and `ota_reboot_pending_` (all owned by `OtaCleanupManager`). This means a Discovery Refresh request and an OTA-triggered cleanup cannot run concurrently — the first one to start owns the path.

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
3. **Delegation-only behavior:** The button performs no cleanup logic itself — all work is delegated to `GeappliancesBridge::trigger_discovery_refresh()`, which forwards to `OtaCleanupManager`.
4. **Queued execution:** The request is queued at press time and executes in `OtaCleanupManager::loop()` when the bridge is ready. No guard checks for steady state, MQTT, or device ID are performed at press time.
5. **Shared cleanup path with OTA:** Both OTA reboot and Discovery Refresh use the same `ota_cleanup_in_progress_` → `ota_discovery_publishing_` → `ota_reboot_pending_` flow (all state owned by `OtaCleanupManager`).

---

## 9. Known Limitations

1. **No visual feedback:** The button provides no indication of whether the press was accepted or rejected. The user must check logs to determine the outcome.
2. **Device reboot is mandatory:** After cleanup completes, the device always reboots. There is no option to complete cleanup without rebooting.
3. **5-second pre-reboot delay is fixed:** The delay between discovery publishing completion and reboot is hardcoded to 5 seconds. If the MQTT broker is slow to acknowledge messages, some messages may be lost.
4. **No cancellation:** Once cleanup has started, there is no way to cancel it. The device will proceed through cleanup, publish, and reboot.
5. **`safe_reboot()` for graceful disconnect:** The reboot uses `App.safe_reboot()` to gracefully disconnect from MQTT before resetting, ensuring a clean session end on the broker.
