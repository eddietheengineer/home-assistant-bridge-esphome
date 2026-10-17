# The Software Factory

How features and bugs flow through this repo: submitted as issues, designed by grilling, implemented test-first, reviewed by parallel sub-agents, verified on the physical test bench, and shipped as a PR with documentation, specs, and ADRs.

This document is the lifecycle source of truth. The skills in `skills/` are the executable form of each stage; when they disagree, fix the skill.

## Lifecycle

```mermaid
flowchart LR
  A[Feature/bug filed<br/>issue template] --> B[Triage<br/>skills/triage]
  B --> C[Grill<br/>skills/grill-with-docs<br/>→ ADRs + glossary]
  C --> D[Spec<br/>skills/to-spec<br/>→ .scratch/feature/SPEC.md]
  D --> E[Tickets<br/>skills/to-tickets<br/>→ .scratch/feature/issues/]
  E --> F[Implement<br/>skills/implement + tdd]
  F --> G[Review<br/>skills/code-review<br/>4 sub-agent axes]
  G --> H[Device verify<br/>device-verify skill<br/>firmware-facing only]
  H -->|fail| F
  H -->|pass| I[PR<br/>skills/pr]
  I --> J[Merge<br/>user action, CI green]
```

Every stage boundary is a **user confirmation gate**: the agent summarizes what the stage produced and waits before proceeding.

## Stages

| Stage | Skill | Produces | Tracker label |
|---|---|---|---|
| Intake | — (issue templates in `.github/ISSUE_TEMPLATE/`) | A structured issue | `needs-triage` |
| Triage | `triage` | Category + state, verified claim, agent brief | `accepted` (or `needs-info` / terminal) |
| Grill | `grill-with-docs` | Settled design tree; ADRs in `docs/adr/`; `docs/GLOSSARY.md` updates | — |
| Spec | `to-spec` | `.scratch/<feature>/SPEC.md`; affected `docs/spec/` modules identified | `ready-for-agent` |
| Tickets | `to-tickets` | `.scratch/<feature>/issues/NN-*.md` with blocking edges | — |
| Implement | `implement` + `tdd` | Working code + tests at agreed seams; module specs updated | `in-progress` |
| Review | `code-review` | Four-axis findings (Standards / Security / Spec / Invariants), resolved or accepted | — |
| Device verify | `device-verify` (local managed skill) | Test-bench evidence posted to the issue | `needs-device-verify` |
| PR | `pr` | PR against `develop` with summary, evidence, merge danger, device verification | labels cleared on merge |

## Verification gauntlet

The checks that must be green before a PR, as they apply:

| Check | Command | When |
|---|---|---|
| Unit + simulation tests | `make test -j4` | Always |
| Full suite incl. startup integration | `make integration-test -j4` | Always (locally; excluded from CI by design) |
| Pipeline tests | `make pytest` | `scripts/ha_discovery/` changed |
| Generated-file sync | `make check-generated` | ERD definitions, overrides, or pipeline scripts changed (rerun the pipeline and commit all generated files together) |
| Real ESPHome build | `esphome compile docs/test-compile.yaml` | `components/geappliances_bridge/` changed |
| Device verification | `device-verify` skill | Diff touches `components/` or `lib/` |

CI (`.github/workflows/`) runs the first three plus the ESPHome compile on every push/PR. Device verification is a manual, in-session loop — it needs the physical test bench and the tailnet, which CI runners don't have.

## Device verification gate

A change is **firmware-facing** iff its diff touches `components/` or `lib/`. Firmware-facing changes require test-bench evidence (MQTT topics observed, values, logs, benchmark numbers) posted to the issue before the PR opens. Non-firmware-facing changes (pipeline, docs, CI, scripts) are exempt; the PR body records the exemption and its reason.

The test bench and its details (device, wiring, HA access, MQTT credentials) are workstation-local and live in the `device-verify` managed skill, not in this repo.

## Definition of done

A feature is done when ALL hold:

- Every acceptance criterion in the spec and tickets is met.
- The verification gauntlet is green (as applicable).
- Code review is clean on all four axes, or findings are explicitly accepted by the user.
- Device verification evidence is posted (firmware-facing) or the exemption is recorded.
- Affected `docs/spec/` module specs are updated (specs are the single source of truth for module contracts).
- ADRs exist for hard decisions; `docs/GLOSSARY.md` is current.
- The PR is open against `develop` with the full `pr`-skill body.

Merging is the user's action, never the agent's.

## Working agreements

- **Branching:** one branch per feature/issue, from `develop`; work happens on the fork, PRs target `develop` on the canonical repo.
- **Never merge, never force-push** without explicit user instruction; no `Co-Authored-By:` trailers.
- **Facts vs decisions:** the agent looks up facts (code, git history, issues) and puts decisions to the user. An agent that answers its own decisions has broken the process.
- **Glossary discipline:** code, specs, issues, PRs, and ADRs use the terms in `docs/GLOSSARY.md`. When code and glossary disagree, the code wins — fix the glossary.
- **Generated files** are never hand-edited; the pipeline reruns and commits atomically with the change that caused them.

## Setup (one-time, per workstation)

```bash
# 1. Link the repo skills into the agent's project skill directory
bash scripts/factory/setup_skills.sh

# 2. Create the triage labels on the canonical repo (owner account only)
bash scripts/factory/setup.sh
```

The `device-verify` skill is a local managed skill (workstation-specific; the test bench doesn't travel with the repo).

## Provenance

The stage skills adapt the discipline from [mattpocock/skills](https://github.com/mattpocock/skills) (grilling, triage, to-spec, to-tickets, implement, code-review, tdd, diagnosing-bugs, pr, domain-modeling), rebound to this repo's conventions: CppUTest + simulation doubles, `docs/spec/` module contracts, the four-axis review, and the physical test-bench gate. `factory` and `device-verify` are original to this repo.
