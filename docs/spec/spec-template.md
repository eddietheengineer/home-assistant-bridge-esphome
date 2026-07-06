# ModuleName — Specification

## 1. Overview

### 1.1 Purpose

[What this module does and why it exists. One to three sentences.]

### 1.2 Responsibilities

- [What it owns. Be specific — name the data structures, state machines, or interfaces.]

### 1.3 Not Responsible For

- [What it deliberately does not do. Name the module that owns it instead.]

---

## 2. Interface

### 2.1 Handle

```c
typedef struct {
  const struct ModuleName_api_t* api;
} module_name_t;
```

[Explain the handle structure and how callers interact with it.]

### 2.2 Vtable

```c
typedef struct ModuleName_api_t {
  void (*init)(module_name_t* self, /* params */);
  /* ... */
} ModuleName_api_t;
```

### 2.3 Public API

| Method | Description |
|--------|-------------|
| `module_name_init()` | [Initialize with params.] |
| `module_name_dothing()` | [Perform operation.] |

---

## 3. Behavior

### 3.1 [Behavior Name]

#### Requirement 3.1.1: [Title]

[State the requirement precisely. Use MUST/SHOULD/MAY per RFC 2119.]

**Rationale:** [Why this requirement exists. What problem it solves.]

**Implementation:** [How it's implemented. Reference source files: `module_name.cpp` lines X–Y.]

**Verification:** [How to test or verify this behavior. Reference tests if applicable.]

---

## 4. Notes

1. [Design decision or tradeoff. Explain the alternative considered and why this was chosen.]