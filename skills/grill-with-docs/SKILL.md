---
name: grill-with-docs
description: "User-invoked. A relentless interview (grilling) about a plan or design that also builds the project's domain model: updates docs/GLOSSARY.md and writes ADRs in docs/adr/ as decisions land. Use when starting a feature or working through a hard design decision."
---

Run a `grilling` session for the whole session (read and follow `skills/grilling/SKILL.md`), with `domain-modeling` discipline active (read and follow `skills/domain-modeling/SKILL.md`).

The difference from plain grilling: as decisions land, write them down immediately, in the same turn.

- **Term resolved or sharpened** → update `docs/GLOSSARY.md`.
- **Hard decision settled** (all three: hard to reverse, surprising without context, result of a real trade-off) → write an ADR in `docs/adr/` per `docs/adr/README.md` and `docs/adr/TEMPLATE.md`, numbered sequentially after the highest existing ADR.
- **No ADR** when any of the three tests fails.

The session ends when the grilling frontier is empty **and** the user confirms shared understanding. Only then may implementation planning (`to-spec`, `to-tickets`) begin.
