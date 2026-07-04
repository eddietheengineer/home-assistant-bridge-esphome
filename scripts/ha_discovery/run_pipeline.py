#!/usr/bin/env python3
"""Regenerate all HA discovery artifacts in sequence.

Usage:
    python3 scripts/ha_discovery/run_pipeline.py [--no-filter]

Steps:
    1. Run auto_detect_scaling on the processed JSON (in-place).
    2. Generate filtered JSONL files.
    3. Generate unfiltered JSONL files (if --no-filter).
    4. Compress filtered JSONL into ha_discovery_data.h.
    5. Compress unfiltered JSONL into ha_discovery_data_unfiltered.inc.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Regenerate all HA discovery artifacts.")
    parser.add_argument("--no-filter", action="store_true",
                        help="Also generate unfiltered variant.")
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    repo_root = script_dir.parent.parent
    generators = script_dir / "generators"
    pipeline = script_dir / "pipeline"
    processed = repo_root / "appliance_api_erd_definitions_processed.json"
    ha_dir = repo_root / "ha_discovery"
    unfiltered_ha_dir = repo_root / "ha_discovery_unfiltered"

    def run(cmd, **kwargs):
        print(f"  {' '.join(str(c) for c in cmd)}", file=sys.stderr)
        subprocess.run(cmd, check=True, **kwargs)

    # Step 1: Auto-detect scaling
    print("Step 1: Auto-detect scaling...", file=sys.stderr)
    run([sys.executable, str(pipeline / "auto_detect_scaling.py"),
         "--input", str(processed), "--output", str(processed)])

    # Step 1.5: Post-process (reapply overrides)
    print("Step 1.5: Post-process...", file=sys.stderr)
    run([sys.executable, str(pipeline / "post_process.py"),
         "--input", str(processed), "--output", str(processed)])

    # Step 2: Generate filtered JSONL
    print("Step 2: Generate filtered JSONL...", file=sys.stderr)
    run([sys.executable, str(generators / "generate_ha_discovery.py"),
         "--processed", str(processed),
         "--output-dir", str(ha_dir)])

    # Step 3: Compress filtered
    print("Step 3: Compress filtered header...", file=sys.stderr)
    run([sys.executable, str(generators / "compress_ha_discovery.py"),
         "--input-dir", str(ha_dir),
         "--header-name", "ha_discovery_data"])

    # Step 4: Unfiltered variant (optional)
    if args.no_filter:
        print("Step 4: Generate unfiltered JSONL...", file=sys.stderr)
        run([sys.executable, str(generators / "generate_ha_discovery.py"),
             "--processed", str(processed),
             "--no-filter",
             "--output-dir", str(unfiltered_ha_dir)])

        print("Step 5: Compress unfiltered header...", file=sys.stderr)
        run([sys.executable, str(generators / "compress_ha_discovery.py"),
             "--input-dir", str(unfiltered_ha_dir),
             "--header-name", "ha_discovery_data_unfiltered",
             "--extension", ".inc"])

    print("Done!", file=sys.stderr)


if __name__ == "__main__":
    main()