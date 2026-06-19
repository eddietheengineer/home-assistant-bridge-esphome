# BridgeMode

## Purpose

Define the `BridgeMode` enumeration as a standalone, shared type so it can be referenced by both `IBridgeServices` and `GeappliancesBridge` without creating a circular dependency.

## Enum Values

| Value | Constant | Description |
|-------|----------|-------------|
| 0 | `BRIDGE_MODE_POLL` | Always use polling mode |
| 1 | `BRIDGE_MODE_SUBSCRIBE` | Always use subscription mode |
| 2 | `BRIDGE_MODE_AUTO` | Auto: try subscription, fallback to polling |

These enum values must match `MODE_*_VALUE` constants in `__init__.py`.

## Dependencies

None.

## Key Design Decisions

- **Standalone header**: Defined in its own file to avoid circular dependencies between `IBridgeServices` and `GeappliancesBridge`.
- **No logic**: This header contains only the enum declaration — no functions, state, or behavior.

## Testing

Exercised through all bridge tests via the mode selection logic.
