# Copilot Instructions

## Pull Request Guidelines

- When a PR is updated with a new commit, the PR description and title must be updated to reflect the context of **all changes in the PR**, not just the changes from the latest commit or request.

## PR Modification API

When modifying PR descriptions or adding PR comments, use the GitHub REST API via `gh api` instead of `gh pr edit` or `gh pr comment`, as the latter use deprecated GraphQL APIs that may fail.

**Update PR description:**
```bash
gh api repos/{owner}/{repo}/pulls/{number} -X PATCH -f body="$(cat body-file.md)"
```

**Add PR comment:**
```bash
gh api repos/{owner}/{repo}/issues/{number}/comments -X POST -f body="$(cat comment-file.md)"
```

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
