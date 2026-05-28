# Stage Gate Checklist

This file is the execution guardrail for the numbered stage docs.

Use it to answer:

- can this stage start yet?
- what must be delivered before it can stop?
- what work is not allowed to spill into this stage?

The goal is to keep implementation reviewable and prevent stage drift.

## Global Rules

- Read `docs/00_control/DECISION_LOG.md` before starting any stage.
- Read `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md` before changing contracts that touch source identity, parsed artifacts, trust, recovery, or shipped surface.
- Read `docs/10_plan/MULTI_FILE_IMPORT_AND_PARSE_WORKFLOW.md` before changing batch import, multi-file loading, import queueing, or no-project batch assignment.
- Do not close a stage without a short closure note.
- Do not begin the next stage just because the current one became inconvenient.
- Do not edit the control docs to resolve implementation difficulty unless a human explicitly asked for a docs/process update.

Each stage closure note should include:

- what shipped
- what was tested
- what intentionally remains for the next stage

Closure note location and naming:

- store closure notes under `docs/90_closure_notes/`
- use the pattern `STAGE_XX_SLICE_YY_CLOSURE.md`
- if a stage completes without formal slices, use `STAGE_XX_CLOSURE.md`

## Stage 01 Gate

Primary doc:

- `docs/archive/10_plan/STAGE_01_FOUNDATION_MAINWINDOW_XTF_REMEDIATION.md`

May start when:

- the decision log is accepted as the current working policy
- the implementation agent is allowed to move behavior out of `MainWindow`
- the implementation agent is allowed to tighten import/cache/metadata contracts

Must deliver:

- one real shell-state path for panel/workspace transitions
- status/context responsibilities separated enough that they no longer overwrite each other
- artifact-store/index consistency across raw, cache, and fallback behavior
- metadata parity across cache-hit and raw-import paths
- simple layer activation that no longer requires full-line decode on the UI thread
- first-pass regression coverage for the highest-risk import/cache paths
- the first real automated test entry point or documented repeatable test command that later stages can extend
- the first real `tests/` + CTest entry point that later stages can extend

Evidence required:

- code changes tied to the Stage 01 doc
- tests or repeatable validation notes for:
  - cache/raw alignment
  - metadata persistence
  - nav backfill
  - stale cache invalidation
- a clear test entry point that Stage 02 can reuse instead of inventing a second harness
- evidence that the harness is integrated into the top-level build, not only a local script
- a closure note listing what Stage 02 may now assume

Must not absorb:

- broad XTF format expansion
- full performance sweep
- final import-once workflow policy

Suggested implementation slices:

- `01A` shell-state unification
- `01B` status channel and shipped-surface cleanup
- `01C` artifact-store/session contract
- `01D` metadata persistence and cache parity
- `01E` activation/loading correction plus tests

## Stage 02 Gate

Primary doc:

- `docs/10_plan/STAGE_02_XTF_COMPATIBILITY_EXPANSION.md`

May start when:

- Stage 01 closure note exists
- artifact-store/index consistency is already defined
- there is at least a minimal parser-fixture test harness

Must deliver:

- a checked-in compatibility matrix
- an initial fixture corpus or fixture manifest
- explicit support states:
  - supported
  - recognized but partial
  - unsupported
- unsupported-format behavior that fails clearly instead of silently mis-parsing
- expanded packet/channel coverage in the highest-value XTF families

Evidence required:

- fixture inventory
- support matrix
- test results tied to representative XTF families
- a closure note listing what is now supported, partial, and still deferred

Must not absorb:

- broad UI shell redesign
- large-scale performance tuning
- final import-once policy work

Suggested implementation slices:

- `02A` compatibility matrix and fixture inventory
- `02B` unsupported-format detection and support-state reporting
- `02C` packet/channel coverage expansion
- `02D` bathymetry or vendor-gap work where product value is highest

## Stage 03 Gate

Primary doc:

- `docs/10_plan/STAGE_03_PERFORMANCE_AND_SCALABILITY.md`

May start when:

- Stage 01 blocker-level performance fixes are already in place or contained
- Stage 02 has produced enough representative file coverage to benchmark real workloads
- the team agrees which workload scenarios matter most

Must deliver:

- repeatable benchmark scenarios
- before/after measurements for the main workload classes
- visible-first loading behavior for heavy data
- interruption or cancellation behavior for long-running activation/loading work
- documented memory-residency policy
- if multi-file import is in scope for the release target:
  - bounded batch queueing that follows `D-14`
  - batch responsiveness behavior that follows `docs/10_plan/MULTI_FILE_IMPORT_AND_PARSE_WORKFLOW.md`

Evidence required:

- benchmark set
- before/after numbers
- workload scenario list
- reference workstation note with hardware, dataset class, and chosen benchmark scenarios
- if multi-file import is in scope:
  - one batch benchmark or repeatable validation scenario
- closure note explaining what improved and what still scales poorly

Must not absorb:

- new compatibility claims that have not been fixture-tested
- import-policy redesign
- registry-model redesign

Suggested implementation slices:

- `03A` benchmark set and baseline capture
- `03B` project-open and layer-activation responsiveness
- `03C` rendering and memory discipline
- `03D` cancellation/interruption and closure report

## Stage 04 Gate

Primary doc:

- `docs/10_plan/STAGE_04_IMPORT_ONCE_WORKFLOW_POLICY.md`

May start when:

- Stage 01 import/cache/source contracts are stable enough to depend on
- Stage 03 responsiveness is good enough that reuse-first behavior feels fast
- the decision log still clearly defines current registry scope and parsed-artifact ownership

Must deliver:

- one explicit import decision flow or state machine
- existing-project lookup before new-project creation
- one preferred existing-layer selection rule
- reuse/reopen/rebuild user messaging
- regression coverage for duplicate-import and reopen scenarios
- if multi-file import is in scope for the release target:
  - batch project assignment that follows `D-11`
  - per-file reuse/rebuild/new behavior inside the batch
  - batch summary messaging

Evidence required:

- tests or repeatable validation for duplicate import and reopen cases
- user-flow notes for:
  - reuse existing parsed data
  - reopen existing project
  - rebuild stale parsed data
  - report missing source
- if multi-file import is in scope:
  - one no-project batch case
  - one mixed reused/rebuilt/new batch case
- closure note explaining what is supported at project/workspace level and what still depends on future registry work

Must not absorb:

- a brand-new company-wide registry service
- broad parser redesign
- broad cache/schema redesign

Suggested implementation slices:

- `04A` import decision flow
- `04B` existing-project lookup
- `04C` existing-layer reuse/rebuild behavior
- `04D` messaging and regression coverage

## Stop Conditions

The implementation agent should stop and ask for direction if:

- a stage depends on a product decision not locked in `docs/00_control/DECISION_LOG.md`
- the work would violate the shipped-surface policy
- the work would require redefining parsed-artifact ownership beyond the locked decision
- the current change clearly belongs to a later stage

## Bottom Line

This file is here to make stage transitions explicit.

If a stage cannot prove it met its gate, it is not done yet.
