# Documentation Style Guide

All documentation in this repository follows these conventions.

## Directory Structure

```
docs/
├── README.md                  # Documentation index (entry point)
├── style-guide.md             # This file
├── architecture/
│   ├── overview.md            # C4 context + container diagrams, system summary
│   ├── startup-sequence.md    # 7-phase startup HSM sequence diagram
│   ├── data-flow.md           # Read path, write path, discovery flow
│   └── roadmap.md             # Future features and architectural improvements
├── spec/                      # One file per module (detailed specifications)
│   └── spec-template.md       # Template for new specs
├── guides/
│   ├── quickstart.md          # Get up and running in 5 minutes
│   ├── deployment.md          # Configuration per appliance type
│   ├── troubleshooting.md     # Common issues and diagnostic flow
│   ├── development.md         # Build, test, debug workflow
│   └── pipeline.md            # HA discovery pipeline end-to-end
└── reference/
    ├── yaml-config.md         # Component YAML configuration reference
    ├── mqtt-topics.md         # MQTT topic schema, payloads, QoS
    ├── erd-protocol.md        # GEA2/GEA3 protocol overview
    └── interfaces.md          # Key C interface documentation
```

## Document Types

### Specifications (`docs/spec/`)

Detailed behavioral contracts for each module. Follow the spec template exactly.

**Required sections** (in order):

1. **Overview** — Purpose, Responsibilities, Not Responsible For
2. **Interface** — Handle struct, vtable, public API table
3. **Behavior** — Numbered requirements with Rationale, Implementation, Verification
4. **Notes** — Design decisions, tradeoffs, known limitations

### Module Descriptions (`doc/module_descriptions/`)

Lightweight summaries for architecture overview. One paragraph of purpose, bullet list of responsibilities, bullet list of dependencies.

### Guides (`docs/guides/`)

Procedural documents. Use imperative voice ("Run the pipeline", "Configure the UART"). Include code blocks with real, tested examples.

### Reference (`docs/reference/`)

Lookup documents. Tables, not prose. Every config option, every topic pattern, every interface method.

### Architecture (`docs/architecture/`)

Diagrams (mermaid) with supporting text. C4 model: context → container → component.

## Formatting Conventions

### Headings

- `#` for document title only (matches filename).
- `##` for top-level sections.
- `###` for subsections.
- No `####` or deeper — restructure if you need that depth.

### Code Blocks

- Always specify language: ````cpp`, ````c`, ````yaml`, ````bash`, ````python`, ````json`.
- File paths in code blocks use the repo root as base: `components/geappliances_bridge/erd_cache.h`.
- For C declarations, show the full signature including qualifiers.

### Mermaid Diagrams

- Use `graph TB` or `graph LR` for structure; `sequenceDiagram` for flows.
- Every diagram must have a caption describing what it shows.
- Keep diagrams to one screen height; split complex diagrams into focused views.

### Tables

- Use for comparisons, config options, API methods, state transitions.
- Include a brief description before the table explaining what it shows.

### Cross-References

- Reference other docs with relative paths: `[ERD Cache spec](./spec/erd_cache.md)`.
- Reference source files with paths: `erd_cache.h` / `erd_cache.cpp`.
- Reference spec sections with `#` anchors: `[see Behavior §3.2](#32-error-handling)`.

### Terminology

| Term | Definition |
|---|---|
| **ERD** | Entity-Relationship Data point, identified by a 16-bit hex ID |
| **GEA2 / GEA3** | GE Appliance protocol versions (serial bus protocols) |
| **Bridge** | The ESPHome component (this project) |
| **Appliance** | The GE appliance connected via serial bus |
| **Host address** | The appliance's address on the GEA bus |
| **Discovery** | Home Assistant MQTT discovery (publishing entity configs) |
| **Pipeline** | The build-time process converting ERD definitions to embedded data |

### Prohibited

- No emojis.
- No marketing language ("powerful", "revolutionary", "best-in-class").
- No vague references ("the system does X") — name the module.
- No inline HTML — use Markdown.
- No trailing whitespace.
- No lines longer than 120 characters (wrap prose, not code blocks).

## Spec Template

All new specs in `docs/spec/` must follow this template. See `docs/spec/spec-template.md` for the full version.

```markdown
# ModuleName — Specification

## 1. Overview

### 1.1 Purpose

[What this module does and why it exists.]

### 1.2 Responsibilities

- [What it owns.]

### 1.3 Not Responsible For

- [What it deliberately does not do.]

---

## 2. Interface

### 2.1 Handle

```c
[handle struct]
```

### 2.2 Vtable / Public API

```c
[vtable or method table]
```

| Method | Description |
|---|---|

---

## 3. Behavior

### 3.1 [Behavior Name]

#### Requirement 3.1.1: [Title]

[Requirement text.]

**Rationale:** [Why.]

**Implementation:** [How it's implemented, with source references.]

**Verification:** [How to test/verify.]

---

## 4. Notes

1. [Design decision or tradeoff.]
```

## Review Checklist

See [Review Checklist](./review-checklist.md) for the pre-commit quality checklist.
