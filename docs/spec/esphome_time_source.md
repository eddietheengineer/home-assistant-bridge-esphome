# esphome_time_source — Specification

## 1. Overview

### 1.1 Purpose

Provide ESPHome's `millis()`-based monotonic clock as an `i_tiny_time_source_t` for use by the tiny library. This is a thin adapter that bridges ESPHome's time API to the tiny library's time source interface.

### 1.2 Responsibilities

- Implement the `i_tiny_time_source_t` vtable backed by `esphome::millis()`.
- Expose a single static instance via `esphome_time_source_init()`.

### 1.3 Not Responsible For

- Any logic beyond returning the current time.
- Time synchronization, NTP, or wall-clock conversion.
- Managing the lifecycle of the time source (the instance is static; there is no destroy).

---

## 2. Interface

### 2.1 Handle

```c
typedef struct {
  const struct i_tiny_time_source_api_t* api;
} i_tiny_time_source_t;
```

The handle is a C struct containing a pointer to the vtable. Callers interact with it via the macro `tiny_time_source_ticks(self)`, which dispatches through the `api` pointer.

### 2.2 Vtable

```c
typedef struct i_tiny_time_source_api_t {
  tiny_time_source_ticks_t (*ticks)(i_tiny_time_source_t* self);
} i_tiny_time_source_api_t;
```

The vtable contains a single function pointer `ticks` that returns the current tick count as `uint16_t`.

### 2.3 Public API

| Method | Description |
|--------|-------------|
| `esphome_time_source_init()` | Returns a pointer to the static `i_tiny_time_source_t` instance. |

---

## 3. Behavior

### 3.1 Time Source Initialization

#### Requirement 3.1.1: Static Instance

`esphome_time_source_init()` MUST return a pointer to a single static `i_tiny_time_source_t` instance. The instance is initialized at compile time with a static vtable pointing to `esphome_time_source_ticks`.

**Rationale:** The tiny library expects a long-lived time source object. A static instance avoids heap allocation and eliminates the need for a destroy function. The adapter has no state beyond the vtable pointer.

**Implementation:** `esphome_time_source.cpp` lines 10–17. A `static` vtable struct (`api`) and a `static` instance struct are defined at file scope. `esphome_time_source_init()` returns `&instance`.

**Verification:** Call `esphome_time_source_init()` multiple times and confirm the returned pointer is identical.

### 3.2 Tick Retrieval

#### Requirement 3.2.1: millis() Backing

The `ticks` callback MUST return `esphome::millis()`, cast to `tiny_time_source_ticks_t` (`uint16_t`). The `self` parameter MUST be ignored (the function is stateless).

**Rationale:** ESPHome's `millis()` provides a monotonic millisecond counter. The tiny library uses this for timeout tracking and timer events. The `uint16_t` return type means the counter wraps at 65,535 ms (~65 seconds); the tiny library's timer infrastructure is designed to handle this wrap.

**Implementation:** `esphome_time_source.cpp` lines 4–8. The function `esphome_time_source_ticks` casts `self` to void and returns `esphome::millis()`.

**Verification:** Confirm that successive calls to `tiny_time_source_ticks()` on the returned instance produce increasing values (modulo wrap).

---

## 4. Notes

1. The return type `tiny_time_source_ticks_t` is `uint16_t`, meaning the tick count wraps every ~65 seconds. This is intentional: the tiny library's timer infrastructure (`tiny_timer_group_t`) handles wrap-around by comparing tick deltas, not absolute values. Using `millis()` directly (rather than a scaled or shifted value) keeps the adapter simple and avoids introducing its own drift.