# Claude Execution Brief

This file is for the implementation agent.

Its job is to tell the agent how to execute the staged plan without drifting across scope, inventing product policy, or hiding uncertainty inside code.

## Startup Assumption

Before using this brief, the implementation agent should already have read:

1. `docs/README.md`
2. `docs/10_plan/STAGE_00_EXECUTION_ORDER.md`
3. `docs/00_control/DECISION_LOG.md`
4. `docs/00_control/STAGE_GATE_CHECKLIST.md`
5. the current numbered stage doc
6. `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md` if the work touches those contracts
7. `docs/10_plan/MULTI_FILE_IMPORT_AND_PARSE_WORKFLOW.md` if the work touches batch import or multi-file loading

## Conflict Resolution

If the docs appear to conflict, use this precedence order:

1. `docs/00_control/DECISION_LOG.md`
2. `docs/00_control/STAGE_GATE_CHECKLIST.md`
3. the active numbered stage doc
4. `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md`
5. this file

If that still does not resolve the issue, stop and report the ambiguity instead of guessing.

## Core Working Rules

- Follow the numbered stage order exactly.
- Do not start a later stage because the current stage became broad or inconvenient.
- Do not invent product policy when `docs/00_control/DECISION_LOG.md` is silent or says to stop.
- Do not contradict the cross-cutting registry/durability constraints.
- Keep the current write scope bounded to the active stage or slice.
- Prefer explicit contracts, tests, and closure notes over TODO comments and implied behavior.
- Do not edit the control docs to make implementation easier unless a human explicitly asked for a docs/process change.
- Do one slice at a time unless a human explicitly asks for a larger bundled pass.

## What To Do Before Starting A Stage

Before changing code for a stage:

- confirm the stage may start by checking `docs/00_control/STAGE_GATE_CHECKLIST.md`
- restate the active stage in one sentence
- choose one bounded slice inside that stage
- list the expected files or modules likely to change
- list the tests or validation evidence you expect to produce

If you cannot do that cleanly, the slice is still too large.

## How To Slice The Work

Use small execution slices.

Preferred slice shape:

- one primary behavioral goal
- one main file cluster
- one test or validation theme
- one closure note

Avoid slices that try to solve:

- architecture cleanup
- parser expansion
- performance tuning
- workflow policy

all at once.

## Recommended Slice Map

### Stage 01

- `01A` shell-state unification
- `01B` status channel split and shipped-surface cleanup
- `01C` artifact-store/session contract
- `01D` metadata persistence and cache parity
- `01E` activation/loading correction with regression coverage

### Stage 02

- `02A` compatibility matrix and fixture inventory
- `02B` unsupported-format detection and support-state reporting
- `02C` packet/channel coverage expansion
- `02D` highest-value vendor or bathymetry gaps

### Stage 03

- `03A` benchmarks and baseline capture
- `03B` project-open and activation responsiveness
- `03C` render and memory discipline
- `03D` cancellation/interruption behavior and performance closure note
- `03E` batch import queueing and bounded concurrency when multi-file behavior is in scope

### Stage 04

- `04A` import decision flow
- `04B` existing-project lookup before new-project creation
- `04C` existing-layer reuse versus rebuild behavior
- `04D` messaging and regression coverage
- `04E` multi-file batch assignment and batch summary behavior when multi-file import is in scope

## When To Stop And Ask Instead Of Coding

Stop and report a blocker if the current slice would require deciding:

- whether dataset identity should become content-hash-first
- whether parsed artifacts are truly shared across projects
- whether a local workspace registry should become a broader company registry
- whether the shipped UI may expose a feature that is still only partial

Do not "solve" those by making quiet assumptions.

## Required Output After Each Slice

After each slice, produce a short closure note with:

- slice id
- what changed
- files touched
- tests or validation performed
- remaining risks
- whether the stage gate is now fully met or which gate items remain open

Write closure notes under:

- `docs/90_closure_notes/`

Use the filename pattern:

- `STAGE_XX_SLICE_YY_CLOSURE.md`

## Implementation Biases

Prefer:

- one source of truth over duplicate state
- explicit state machines over ad hoc branching
- visible-first loading over eager full-data hydration
- honest unsupported behavior over plausible wrong behavior
- regression coverage over confidence by inspection only

Avoid:

- adding more policy into `MainWindow` unless the slice is explicitly about removing that policy later
- treating cache and raw artifact paths as interchangeable
- widening scope because "the next change is nearby"
- leaving shipped UI actions clickable if they are not really implemented

## Evidence Expectations

For parser/import behavior:

- add or run regression coverage where the stage expects it

For performance work:

- report actual before/after numbers or repeatable validation steps

For workflow-policy work:

- report the exact decision flow that now happens for reuse, reopen, rebuild, and missing-source cases

## Completion Rule

Do not declare a stage complete only because the code looks cleaner.

A stage is complete only when:

- its checklist gate is met
- its required evidence exists
- its closure note states what later stages may safely assume

## Bottom Line

Your job is not only to improve the code.

Your job is to implement the staged plan in a way that stays:

- reviewable
- testable
- scoped
- faithful to the product decisions already made
