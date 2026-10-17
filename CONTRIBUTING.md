# Contributing

## Development Workflow

### Prerequisites

- **ESP-IDF** (for firmware builds)
- **Python 3.10+** (for pipeline scripts and tests)
- **CppUTest** (for unit tests):
  - Ubuntu/Debian: `sudo apt-get install cpputest libcpputest-dev`
  - macOS: `brew install cpputest`

### Clone with Submodules

```bash
git clone --recursive https://github.com/joshualongenecker/home-assistant-bridge-esphome.git
cd home-assistant-bridge-esphome
```

### Build and Test

```bash
# Run unit tests
make test

# Run Python tests
make pytest
```

See [Development Guide](./docs/guides/development.md) for the full workflow including ESPHome compilation and simulation testing.

## Pull Request Process

1. **Fork and branch** from `develop`.
2. **Make your changes.** Follow existing patterns — grep the codebase before inventing a new convention.
3. **Run tests.** `make test` and `make pytest` must pass.
4. **Run the pipeline** if you modified ERD definitions, overrides, or pipeline scripts:
   ```bash
   python3 scripts/ha_discovery/run_pipeline.py
   ```
   Commit all generated files (`ha_discovery/*.jsonl`, `components/geappliances_bridge/ha_discovery_data.h`).
5. **Update documentation** if your change affects behavior, configuration, or architecture. See [docs/style-guide.md](./docs/style-guide.md).
6. **Open a PR** against `develop` with a clear description of what changed and why.

## Code Style

- **C++**: Follow the existing codebase style (4-space indent, braces on same line, `camelCase` for functions, `snake_case` for variables).
- **C**: Same as C++ but use `snake_case` for all identifiers.
- **Python**: PEP 8.
- **No trailing whitespace.** No lines > 120 characters in prose.

### ESPHome Developer-Doc Compliance

The external component in `components/geappliances_bridge/` follows the
ESPHome developer documentation requirements, verified against
`script/ci-custom.py` rules. Intentional deviations (documented, not
oversights):

- **`GEA_TAG` macro** instead of `ESPHOME_LOG_TAG` — the latter does not
  exist in the supported ESPHome version (2026.6.2); `GEA_TAG` expands to
  the same `static const char* const` form.
- **C-style `typedef struct` / `extern "C"`** in data-path files — kept for
  compatibility with the C protocol libraries; C++-only files use `using`.
- **`AUTO_LOAD = ["sensor", "button"]`** in `__init__.py` — required because
  diagnostic entities are published as sensor/button platforms.
- **GEA2 10 ms burst loop** in `run_protocol_stack_()` — services the GEA2
  interface at ~1 ms intervals during the TX→RX cycle (~4 ms at 19200 baud)
  to keep the 6 ms interbyte timeout from firing. 10 ms is under the 50 ms
  `WARN_IF_BLOCKING_OVER_CS` threshold; the loop feeds `esp_task_wdt_reset()`
  each iteration.
- **~44 KB heap allocation** in `ha_discovery_manager_start()` — discovery
  buffers are allocated once at startup, freed in the same run, and the
  device reboots immediately after discovery completes.
- **`HA_CLEANUP_TOPIC_BUF_SIZE` conditional `#define`** — the test build
  overrides the size via `-DHA_CLEANUP_TEST_BUF_SIZE=512`; a `constexpr`
  cannot be overridden from the command line.
- **Vendored `miniz*`** — third-party; modifications include the `gea_tinfl_*`
  rename (avoids an ESP-IDF ROM symbol collision), a version bump to 11.3.1,
  and the hand-written `miniz_export.h` stub. The `#pragma once` guards are
  upstream's own.
- **`ble master` / `bluetooth master`** strings in `FILTER_KEYWORDS` —
  appliance-protocol data, not code identifiers.

## Documentation

All documentation lives in `docs/`. The [style guide](./docs/style-guide.md) defines formatting conventions and the spec template.

**Before committing any behavioral change, update the relevant spec.** Specs are the single source of truth for module contracts.

## Adding Overrides to the HA Discovery Pipeline

If an ERD needs a manual override (wrong domain, missing unit, incorrect scaling), add it to `OVERRIDES` in `scripts/ha_discovery/pipeline/post_process.py`. See [.github/copilot-instructions.md](./.github/copilot-instructions.md) for override format and valid keys.

After adding an override, run the pipeline and commit all generated files.