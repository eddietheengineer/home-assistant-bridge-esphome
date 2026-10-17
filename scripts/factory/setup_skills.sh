#!/usr/bin/env bash
# Link the repo's factory skills into the agent's project skill directory.
# Idempotent: re-run after pulling a branch with new/changed skills.
#
#   OMP_SKILLS_DIR=/path bash scripts/factory/setup_skills.sh   # override target
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

SKILL_DIR="${OMP_SKILLS_DIR:-.omp/skills}"
mkdir -p "$SKILL_DIR"

for skill in skills/*/; do
  name="$(basename "$skill")"
  link="$SKILL_DIR/$name"
  if [ -e "$link" ] && [ ! -L "$link" ]; then
    echo "skip: $link exists and is not a symlink (refusing to overwrite)" >&2
    continue
  fi
  ln -sfn "../../skills/$name" "$link"
  echo "linked: $link -> skills/$name"
done

echo "Done. New skills appear in the next agent session."
