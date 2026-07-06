# Bridge Mode — Specification

## 1. Overview

### 1.1 Purpose

Defines the `BridgeMode` enumeration as a standalone, shared type so it can be referenced by both `IBridgeServices` and `GeappliancesBridge` without creating a circular dependency.

### 1.2 Responsibilities

- Declare the `BridgeMode` enum with three values: `BRIDGE_MODE_POLL`, `BRIDGE_MODE_SUBSCRIBE`, `BRIDGE_MODE_AUTO`.
- Provide the mapping comment tying enum values to `__init__.py` constants.

### 1.3 Not Responsible For

- Any logic, state, or behaviour related to bridge mode selection or switching.
- Runtime mode negotiation or fallback logic (owned by the startup sequence and bridge implementations).

---

## 2. Interface

### 2.1 Enum

```c
enum BridgeMode {
  BRIDGE_MODE_POLL      = 0,  // Always use polling mode
  BRIDGE_MODE_SUBSCRIBE = 1,  // Always use subscription mode
  BRIDGE_MODE_AUTO      = 2   // Auto: try subscription, fallback to polling
};
```

The enum is declared within the `esphome::geappliances_bridge` namespace.

### 2.2 Enum Values

| Value | Constant | Description |
|-------|----------|-------------|
| `0` | `BRIDGE_MODE_POLL` | Always use polling mode. The bridge reads ERD values on a schedule rather than relying on appliance-pushed updates. |
| `1` | `BRIDGE_MODE_SUBSCRIBE` | Always use subscription mode. The bridge requests the appliance to push ERD updates; if the appliance does not support subscriptions, the bridge will fail rather than fall back. |
| `2` | `BRIDGE_MODE_AUTO` | Auto-detect. The bridge first attempts subscription mode; if the appliance does not support it, the bridge falls back to polling. |

### 2.3 Contract with Python Configuration

The enum values MUST match the `MODE_*_VALUE` constants defined in `__init__.py` (the ESPHome platformio component). The YAML configuration key maps to these integer values, which are then passed to the C++ layer as a `BridgeMode` enum.

---

## 3. Behavior

### 3.1 Mode Selection

#### Requirement 3.1.1: Enum Value Stability

The integer values of `BridgeMode` MUST NOT change without a corresponding update to the `MODE_*_VALUE` constants in `__init__.py`. A mismatch between the Python-side constant and the C++ enum value will cause incorrect mode selection at runtime.

**Rationale:** The bridge mode is configured in YAML, parsed by ESPHome's Python layer, and passed as an integer to the compiled component. The enum serves as the single source of truth for the mapping.

**Implementation:** `components/geappliances_bridge/bridge_mode.h` lines 26–30.

**Verification:** Confirm that `MODE_POLL_VALUE`, `MODE_SUBSCRIBE_VALUE`, and `MODE_AUTO_VALUE` in `__init__.py` match `0`, `1`, and `2` respectively.

---

## 4. Notes

1. **Standalone header to break circular dependency.** `IBridgeServices` and `GeappliancesBridge` both need to reference `BridgeMode`, but neither can include the other's header. This header has no dependencies and can be included by either party.