# Copilot Instructions

## Pull Request Guidelines

- When a PR is updated with a new commit, the PR description and title must be updated to reflect the context of **all changes in the PR**, not just the changes from the latest commit or request.

## Pipeline Overrides

If an ERD requires a manual override (wrong domain, missing unit, incorrect scaling), add it to the `OVERRIDES` dict in `scripts/ha_discovery/pipeline/post_process.py`. This is reapplied after auto-detection so overrides survive subsequent pipeline runs.

Format:

```python
OVERRIDES = {
    "0x7130": {"ha_domain": "sensor", "unit_of_measurement": "rpm"},
    # ...
}
```

After adding an override, run `python3 scripts/ha_discovery/run_pipeline.py` and commit all generated files.
