# Discovery Refresh Button

## Purpose

ESPHome button component that triggers a Home Assistant discovery cleanup and device restart. When pressed, it cleans up stale discovery messages from the MQTT broker and restarts the device so that normal discovery republishes a fresh set of entities. This is useful when entities become stale or out of sync with the appliance's actual capabilities.

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

`GeappliancesBridge::trigger_discovery_refresh()` performs three guard checks before starting cleanup:

1. **Already in progress**: If `discovery_refresh_in_progress_` is `true`, the request is ignored with a warning.
2. **Not in steady state**: If `steady_state_reached_` is `false`, the request is rejected — discovery refresh requires the bridge to be fully initialized.
3. **Discovery manager busy**: If `ha_discovery_manager_is_processing()` returns `true`, the request is rejected — a previous run must finish first.

If all guards pass, the cleanup module is configured and started:
- `ha_discovery_cleanup_configure()` sets up the cleanup module with the device ID, MQTT client, and `esphome::millis` as the time source
- `ha_discovery_cleanup_start()` begins the cleanup process
- `discovery_refresh_in_progress_` is set to `true`

The cleanup runs incrementally in the bridge's `update()` loop:
- `ha_discovery_cleanup_run()` processes cleanup work
- When `ha_discovery_cleanup_is_done()` returns `true`:
  - The cleanup module is destroyed (to unsubscribe the wildcard topic and prevent use-after-free)
  - The watchdog is fed, then a 500 ms delay precedes `esphome::App.reboot()`

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
- `geappliances_bridge.h` — `GeappliancesBridge` class (owns `trigger_discovery_refresh()`)
- `ha_discovery_cleanup.h` — cleanup module for removing stale MQTT discovery messages
- `device_identity_manager.h` — provides the device ID for cleanup configuration

## Key Design Decisions

- **Thin wrapper**: The class is a minimal adapter between ESPHome's button component and the bridge's `trigger_discovery_refresh()` method. All logic (guards, cleanup, restart) lives in `GeappliancesBridge`.
- **Null-safe**: The `press_action()` method checks `bridge_ != nullptr` before calling through, protecting against use-after-free if the bridge is destroyed before the button.
- **Auto-created**: The button is created by default (`discovery_refresh_button: true`) so users get the functionality without explicit configuration. It can be disabled by setting `discovery_refresh_button: false`.
- **Reboot on completion**: After cleanup finishes, the device restarts to republish all discovery messages cleanly. This is simpler than trying to selectively republish and ensures a consistent state.
- **Watchdog protection**: The 500 ms delay before reboot exceeds the default TWDT timeout (30 ms). The watchdog is explicitly reset before the delay to prevent an unintended reset.
- **Cleanup destroy before reboot**: The cleanup module is destroyed before reboot to unsubscribe the wildcard MQTT topic. If the callback fires after teardown zeroes the struct, it could corrupt heap metadata.