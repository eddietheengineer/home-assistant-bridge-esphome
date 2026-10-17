---
name: code-review
description: "Model-invoked. Review the diff since a fixed point along four axes — Standards, Security, Spec, Invariants — run as parallel sub-agents, aggregated side by side. Use when the user asks to review a branch, PR, or work-in-progress changes."
---

Four-axis review of the diff between `HEAD` and a fixed point the user supplies:

- **Standards**: does the code conform to this repo's documented coding standards?
- **Security**: evidence-backed security findings.
- **Spec**: does the code faithfully implement the originating issue / spec?
- **Invariants**: does the code respect the repo's load-bearing invariants?

All four axes run as **parallel sub-agents** (one `task` batch) so they don't pollute each other's context, then this skill aggregates their findings.

## Process

### 1. Pin the fixed point

Whatever the user said (a commit SHA, branch name, tag, `develop`, `HEAD~5`). If they didn't specify one, ask. Capture the diff command once: `git diff <fixed-point>...HEAD` (three-dot, against the merge-base) and the commit list via `git log <fixed-point>..HEAD --oneline`. Confirm the fixed point resolves and the diff is non-empty before spawning anything.

### 2. Identify the spec source

In this order:

1. Issue references in the commit messages (`#123`, `Closes #45`), fetched via `gh issue view`.
2. A path the user passed.
3. `.scratch/<feature>/SPEC.md` matching the branch or feature.
4. If nothing is found, ask. If there isn't one, the Spec axis reports "no spec available" and is skipped.

### 3. Identify the standards sources

`CONTRIBUTING.md`, `docs/style-guide.md`, `.github/copilot-instructions.md` (Code Style + Global Protocol Rules sections). On top of whatever the repo documents, the Standards axis always carries the **smell baseline** below (Fowler, _Refactoring_ ch.3). Two rules bind it: **the repo overrides** (a documented standard wins; suppress the smell where it endorses what the baseline flags) and **always a judgement call** (labelled heuristic, never a hard violation; skip anything tooling enforces).

- **Mysterious Name**: a name that doesn't reveal what it does or holds. → rename; if no honest name comes, the design's murky.
- **Duplicated Code**: the same logic shape in more than one hunk or file. → extract the shared shape.
- **Feature Envy**: a method that reaches into another object's data more than its own. → move it onto the data it envies.
- **Data Clumps**: the same few fields/params travelling together. → bundle into one type.
- **Primitive Obsession**: a primitive standing in for a domain concept. → give the concept its own small type.
- **Repeated Switches**: the same switch/if-cascade on the same type recurs. → polymorphism or one shared map.
- **Shotgun Surgery**: one logical change forces scattered edits. → gather what changes together.
- **Divergent Change**: one module edited for several unrelated reasons. → split it.
- **Speculative Generality**: abstraction added for needs the spec doesn't have. → delete it.
- **Message Chains**: long `a.b().c().d()` navigation. → hide the walk behind one method.
- **Middle Man**: a function that mostly delegates onward. → cut it.
- **Refused Bequest**: an implementer that ignores most of what it inherits. → composition.

### 4. Spawn all four sub-agents in parallel

One `task` batch, four agents:

**Standards** → `reviewer` sub-agent. Include: the diff command and commit list; the standards-source files; the smell baseline pasted in full. Brief: "Report, per file/hunk where relevant, (a) every place the diff violates a documented standard — cite the standard (file + rule); and (b) any baseline smell — name it and quote the hunk. Distinguish hard violations from judgement calls; a documented repo standard overrides the baseline. Skip anything tooling enforces. Under 400 words."

**Security** → `security-reviewer` sub-agent. Include: the diff command and commit list. Brief: "Evidence-backed security findings only: buffer handling (the ERD data path memcpy's into a fixed arena — check bounds), integer scaling/overflow in value conversion, injection via MQTT payload parsing, secrets in logs or discovery payloads. Quote the hunk for each finding. Under 300 words."

**Spec** → `task` sub-agent. Include: the diff command and commit list; the spec contents. Brief: "Report: (a) requirements the spec asked for that are missing or partial; (b) behaviour in the diff that wasn't asked for (scope creep); (c) requirements that look implemented but where the implementation looks wrong. Quote the spec line for each finding. Under 400 words."

**Invariants** → `task` sub-agent. Include: the diff command and commit list; the **Critical Invariants** and **Global Protocol Rules** sections of `.github/copilot-instructions.md` pasted in full; the list of ADRs in `docs/adr/` (accepted) relevant to the touched area. Brief: "Check the diff against each invariant and protocol rule: (erd, board_address) keying everywhere a cache key appears; 0xFF never stored as a physical board address; no mutex held across an MQTT publish; no data_size above 248; generated files (erd_lists, appliance_api_feature_lists, appliance_type_map.h, ha_discovery_data, ha_discovery/*.jsonl, processed JSON) not hand-edited; no file-scope .cpp variables referenced from headers. Report each violation with the invariant cited and the hunk quoted. Under 300 words."

### 5. Aggregate

Present the four reports under `## Standards`, `## Security`, `## Spec`, `## Invariants` headings, verbatim or lightly cleaned. Do **not** merge or rerank findings — the axes are deliberately separate. End with a one-line summary: total findings per axis and the worst issue within each axis (if any).

## Why four axes

A change can pass three and fail the fourth:

- Code that follows every standard, implements the spec, and is secure, but holds the cache mutex across a publish → **Invariants fail.**
- Code that respects every invariant but implements the wrong thing → **Spec fail.**

Reporting them separately stops one axis from masking the others. Nothing commits until the findings are resolved or explicitly accepted by the user.
