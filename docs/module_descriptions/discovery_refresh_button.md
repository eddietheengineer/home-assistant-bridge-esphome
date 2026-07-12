# Discovery Refresh Button

## Purpose

ESPHome button component that triggers a Home Assistant discovery cleanup, republishes fresh discovery topics, and restarts the device. When pressed, the request is queued — if the bridge is not yet ready (steady state, MQTT connected, device ID complete), it waits until ready before starting cleanup. After cleanup, fresh discovery topics are published, then the device reboots to defragment the heap.

## Public API

| Function | Description |
|----------|-------------|
| `DiscoveryRefreshButton(bridge)` | Constructor. Stores a pointer to the `GeappliancesBridge` instance. |
| `press_action()` override | Called by ESPHome when the button is pressed. Delegates to `bridge_->trigger_discovery_refresh()`. |

## Class

```cpp
class DiscoveryRefreshButton : public button::Button {
 public:
  DiscoveryRefreshButton(GeappliancesBridge* bridge) : bridge_(bridge) {}

  void press_action() override {
    if (bridge_ != nullptr) {
      bridge_->trigger_discovery_refresh();
    }
  }

 private:
  GeappliancesBridge* bridge_;
};
```

## Trigger Flow

`GeappliancesBridge::trigger_discovery_refresh()` is a **public** method (no `friend` declaration needed). It delegates to `ota_cleanup_manager_.trigger_discovery_refresh()`, which sets `discovery_refresh_in_progress_` (owned by `OtaCleanupManager`) to `true` and logs that the request is queued. It does **not** perform guard checks or start cleanup immediately — the request is queued and will execute once the bridge is ready.

In the bridge's `loop()`, `ota_cleanup_manager_.loop()` is called, which checks the queued request:

1. **Wait for readiness:** The manager waits until `steady_state_reached_` is `true`, `mqtt_client_adapter_initialized_` is `true`, and `device_identity_manager_.get_state() == DEVICE_ID_STATE_COMPLETE`.
2. **Start cleanup:** Once ready, `ha_discovery_cleanup_configure()` and `ha_discovery_cleanup_start()` are called, `discovery_refresh_in_progress_` is cleared, and `ota_cleanup_in_progress_` is set to `true`.
3. **Drive cleanup:** `ha_discovery_cleanup_run()` processes cleanup work each loop iteration.
4. **On cleanup complete:** The cleanup module is destroyed, fresh discovery topics are published via `ha_discovery_manager_configure()` and `ha_discovery_manager_start()`, and `ota_discovery_publishing_` is set.
5. **Drive discovery publishing:** `ha_discovery_manager_run()` publishes discovery payloads each loop iteration.
6. **On discovery complete:** `mark_boot_successful_for_reboot()` clears the safe mode counter and cancels OTA rollback, then `ota_reboot_pending_` is set.
7. **Wait then reboot:** After a 5-second delay, `esphome::App.safe_reboot()` performs a graceful reboot (disconnects from MQTT before resetting).

The cleanup → publish → reboot flow is driven by `OtaCleanupManager`. The same path is used for automatic discovery change detection (hash or device ID change detected at steady state) and fresh install discovery publish.

## ESPHome Configuration

The button is auto-created by default in `__init__.py`:

```yaml
geappliances_bridge:
  discovery_refresh_button: true  # default; set to false to disable
```

Or with custom options:

```yaml
geappliances_bridge:
  discovery_refresh_button:
    name: "Discovery Refresh"
    disabled_by_default: false
```

## Dependencies

- `esphome/components/button/button.h` — ESPHome `button::Button` base class
- `geappliances_bridge.h` — `GeappliancesBridge` class (public `trigger_discovery_refresh()` delegates to `OtaCleanupManager`)
- `ota_cleanup_manager.h` — `OtaCleanupManager` owns the cleanup → publish → reboot state machine
- `ha_discovery_cleanup.h` — cleanup module for removing stale MQTT discovery messages
- `device_identity_manager.h` — provides the device ID for cleanup configuration

## Key Design Decisions

- **Thin wrapper**: The class is a minimal adapter between ESPHome's button component and the bridge's `trigger_discovery_refresh()` method. All logic (queuing, cleanup, republish, restart) lives in `OtaCleanupManager`, delegated through the bridge.
- **Null-safe**: The `press_action()` method checks `bridge_ != nullptr` before calling through, protecting against use-after-free if the bridge is destroyed before the button.
- **Auto-created**: The button is created by default (`discovery_refresh_button: true`) so users get the functionality without explicit configuration. It can be disabled by setting `discovery_refresh_button: false`.
- **Queued execution**: The request is queued if pressed before the bridge is ready. This eliminates guard checks and allows the user to press the button at any time.
- **Shared cleanup path**: Discovery Refresh, automatic discovery change detection (hash/device_id change at steady state), and fresh install all share the same cleanup → publish → reboot flow (state `ota_cleanup_in_progress_`, `ota_discovery_publishing_`, `ota_reboot_pending_` owned by `OtaCleanupManager`). Fresh install publishes only (no cleanup or reboot).
- **`safe_reboot()` instead of `App.reboot()`**: Uses `App.safe_reboot()` to gracefully disconnect from MQTT before resetting, ensuring a clean session end.
- **`mark_boot_successful_for_reboot()`**: Clears the safe mode boot loop counter and cancels OTA rollback before rebooting, preventing the device from entering safe mode due to rapid reboots.
- **5-second pre-reboot delay**: Allows final discovery messages to transmit and the heap to stabilize before rebooting. The reboot also defragments the heap after the memory-intensive cleanup and publish cycle.