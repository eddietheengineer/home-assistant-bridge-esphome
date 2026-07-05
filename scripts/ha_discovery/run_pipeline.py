#!/usr/bin/env python3
"""Regenerate all HA discovery artifacts in sequence.

Usage:
    python3 scripts/ha_discovery/run_pipeline.py

Steps:
    1. Run auto_detect_pairings on the processed JSON (in-place).
    2. Run auto_detect_ha_domain on the processed JSON (in-place).
    3. Run auto_detect_device_class on the processed JSON (in-place).
    4. Run auto_detect_scaling on the processed JSON (in-place).
    5. Run auto_detect_state_class on the processed JSON (in-place).
    6. Post-process (reapply overrides).
    7. Generate JSONL files to ha_discovery/.
    8. Compress JSONL into ha_discovery_data.h.

Idempotency: the pipeline is safe to re-run. Auto-detection scripts may
clear stale values (e.g. device_class for fields that no longer match),
and post_process re-applies all overrides at the end. Running the pipeline
multiple times produces the same result as a single run.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent.parent
    generators = script_dir / "generators"
    pipeline = script_dir / "pipeline"
    processed = script_dir / "appliance_api_erd_definitions_processed.json"
    ha_dir = repo_root / "ha_discovery"

    def run(cmd, step_name):
        print(f"  {' '.join(str(c) for c in cmd)}", file=sys.stderr)
        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError as e:
            print(f"ERROR: {step_name} failed with exit code {e.returncode}", file=sys.stderr)
            raise

    # Helper: run a pipeline script with atomic write (temp file + rename).
    def run_pipeline_script(script_name, step_name):
        script = pipeline / script_name
        # Write to a temp file in the same directory, then atomically rename.
        fd, tmp_path = tempfile.mkstemp(
            suffix='.json', dir=str(processed.parent), prefix='.tmp_' + processed.name
        )
        os.close(fd)
        try:
            run(
                [sys.executable, str(script),
                 "--input", str(processed), "--output", tmp_path],
                step_name,
            )
            shutil.move(tmp_path, str(processed))
        except Exception:
            # Clean up temp file on failure.
            try:
                Path(tmp_path).unlink(missing_ok=True)
            except OSError:
                pass
            raise

    # Step 1: Auto-detect pairings
    print("Step 1: Auto-detect pairings...", file=sys.stderr)
    run_pipeline_script("auto_detect_pairings.py", "auto_detect_pairings")

    # Step 2: Auto-detect ha_domain
    print("Step 2: Auto-detect ha_domain...", file=sys.stderr)
    run_pipeline_script("auto_detect_ha_domain.py", "auto_detect_ha_domain")

    # Step 3: Auto-detect device_class
    print("Step 3: Auto-detect device_class...", file=sys.stderr)
    run_pipeline_script("auto_detect_device_class.py", "auto_detect_device_class")

    # Step 4: Auto-detect scaling
    print("Step 4: Auto-detect scaling...", file=sys.stderr)
    run_pipeline_script("auto_detect_scaling.py", "auto_detect_scaling")

    # Step 5: Auto-detect state_class
    print("Step 5: Auto-detect state_class...", file=sys.stderr)
    run_pipeline_script("auto_detect_state_class.py", "auto_detect_state_class")

    # Step 6: Post-process (reapply overrides)
    print("Step 6: Post-process...", file=sys.stderr)
    run_pipeline_script("post_process.py", "post_process")

    # Step 7: Generate JSONL
    print("Step 7: Generate JSONL...", file=sys.stderr)
    run([sys.executable, str(generators / "generate_ha_discovery.py"),
         "--processed", str(processed),
         "--output-dir", str(ha_dir)], "generate_ha_discovery")

    # Step 8: Compress into ha_discovery_data.h
    print("Step 8: Compress header...", file=sys.stderr)
    run([sys.executable, str(generators / "compress_ha_discovery.py"),
         "--input-dir", str(ha_dir),
         "--header-name", "ha_discovery_data"], "compress_ha_discovery")

    print("Done!", file=sys.stderr)


if __name__ == "__main__":
    main()