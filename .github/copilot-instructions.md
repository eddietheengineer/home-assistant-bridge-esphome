# Copilot Instructions

## Pull Request Guidelines

- When a PR is updated with a new commit, the PR description and title must be updated to reflect the context of **all changes in the PR**, not just the changes from the latest commit or request.

## HA Discovery Pipeline

**Any change to ERD definitions, overrides, or pipeline scripts requires a full pipeline rerun before committing.**

After modifying any of these files, run:

```bash
python3 scripts/ha_discovery/run_pipeline.py
```

Then commit **all** generated/changed files together:
- `scripts/ha_discovery/appliance_api_erd_definitions_processed.json`
- `ha_discovery/*.jsonl`
- `components/geappliances_bridge/ha_discovery_data.h`

### Adding Overrides

If an ERD requires a manual override (wrong domain, missing unit, incorrect scaling, forced classification), add it to the `OVERRIDES` dict in `scripts/ha_discovery/pipeline/post_process.py`. Overrides are reapplied after auto-detection so they survive subsequent pipeline runs.

Format:

```python
OVERRIDES = {
    "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
    # ...
}
```

After adding an override, run the pipeline and commit all generated files.
