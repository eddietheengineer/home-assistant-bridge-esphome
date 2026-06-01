# Zoneline Subscription ERD Publishing Fix

## Problem

On the `planRefactor` branch, the Zoneline adapter (zonelinec6) detected subscription
activity from the appliance, but no ERD state messages were being published to MQTT.
The device appeared healthy in logs (subscription mode working) but Home Assistant
received no ERD updates.

## Root Cause

The `ApplianceSideStateMachine` was created in `initialize_mqtt_client_()` during the
MQTT client init phase. The `GlobalStateRegistry` had its appliance address set in the
same function, BEFORE the FSM was created.

When `loop()` was called, the FSM checked `registry_->get_appliance_address() != 0`
and transitioned from IDLE to RUNNING. This triggered `start_handlers()`, which called
`subscription_handler_->start()` with an **uninitialized** `ApplianceSideConfig` struct.

The `ApplianceSideConfig` struct contains boolean members (`enable_subscriptions`,
`enable_polling`) that were **uninitialized** (garbage values). When `start_handlers()`
checked `config_.enable_subscriptions`, it read garbage memory - likely `false`, so the
subscription handler was never actually started by the FSM.

Later, `initialize_mqtt_bridge_()` called `subscription_handler_->start()` explicitly
as a workaround (added in a previous fix). But then the FSM's `start_handlers()` called
`start()` AGAIN, re-initializing the HSM and disrupting the subscription.

## Fix

Three changes in `appliance_side_state_machine.h` and `appliance_side_state_machine.cpp`:

1. **Added `config_set_` guard**: A boolean flag that starts as `false` and is set to
   `true` only after `set_config()` is called. The FSM only transitions from IDLE to
   RUNNING when BOTH `config_set_` is true AND the appliance address is non-zero.

2. **Zero-initialized `ApplianceSideConfig`**: Changed the constructor initializer from
   `config_()` (which doesn't zero-initialize non-trivial structs in all compilers) to
   `config_{false, false, 0, false, {}, {}}` (explicit aggregate initialization).

3. **Removed duplicate `start()` calls**: Removed the explicit
   `subscription_handler_->start()` calls from `initialize_mqtt_bridge_()` in both
   SUBSCRIBE and AUTO modes. The FSM now handles starting the handler correctly after
   `set_config()` is called.

## Files Changed

- `components/geappliances_bridge/appliance_side_state_machine.h`
  - Added `bool config_set_{false}` member

- `components/geappliances_bridge/appliance_side_state_machine.cpp`
  - Constructor: `config_{false, false, 0, false, {}, {}}` (explicit zero-init)
  - Constructor: `config_set_(false)`
  - `set_config()`: sets `config_set_ = true`
  - `loop()` IDLE state: requires `config_set_ && address != 0`
  - `on_appliance_address_changed()`: requires `config_set_` before transition

- `components/geappliances_bridge/geappliances_bridge_bridge_init.cpp`
  - Removed `subscription_handler_->start()` from SUBSCRIBE mode block
  - Removed `subscription_handler_->start()` from AUTO mode block

- `test/tests/appliance_side_state_machine_test.cpp`
  - Updated `transitions_to_running_on_address` test to call `set_config()` first
  - Added `stays_idle_without_config` test to verify the guard works
  - Updated `stays_idle_without_address` test to call `set_config()` first

## Verification

All 204 unit tests pass. All 7 device configs compile successfully. OTA flash of
zonelinec6 shows subscription ERD messages being published to MQTT continuously
(`drain: published 1 ERDs` appearing every few seconds in the debug log).
