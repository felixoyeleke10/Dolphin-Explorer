# Stage 00 - Execution Order

Use `docs/README.md` as the navigation entry point.

Use the docs in this order:

## Preparation Docs

Read these before starting any numbered stage:

- `docs/00_control/DECISION_LOG.md`
- `docs/00_control/STAGE_GATE_CHECKLIST.md`
- `docs/00_control/CLAUDE_EXECUTION_BRIEF.md`

These are control docs.

- `docs/00_control/DECISION_LOG.md` tells the implementation agent which product choices are already made.
- `docs/00_control/STAGE_GATE_CHECKLIST.md` defines when a stage may start and what must exist before it is considered complete.
- `docs/00_control/CLAUDE_EXECUTION_BRIEF.md` tells the implementation agent how to execute the plan without drifting across scope.

## Precedence Rules

If two docs appear to disagree, resolve them in this order:

1. `docs/00_control/DECISION_LOG.md`
2. `docs/00_control/STAGE_GATE_CHECKLIST.md`
3. the active numbered stage doc
4. `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md`
5. `docs/00_control/CLAUDE_EXECUTION_BRIEF.md`

How to interpret that order:

- `docs/00_control/DECISION_LOG.md` controls product-policy choices
- `docs/00_control/STAGE_GATE_CHECKLIST.md` controls when a stage may start and stop
- the active numbered stage doc controls the intended scope and deliverables of that stage
- the cross-cutting doc defines constraints and risk areas, but it should not override locked product decisions
- the execution brief controls agent behavior, not product design

## Archived Stage 01

`docs/archive/10_plan/STAGE_01_FOUNDATION_MAINWINDOW_XTF_REMEDIATION.md`

Purpose:

- fix architectural drift in `MainWindow`
- harden XTF/import/cache contracts
- address metadata persistence and loading strategy

Why it came first:

- later workflow policy depends on stable import/cache behavior
- later UX changes should not be built on inconsistent shell state

Use this doc for historical context only. Active stage execution now continues from Stage 02 onward.

## Stage 02

`docs/10_plan/STAGE_02_XTF_COMPATIBILITY_EXPANSION.md`

Purpose:

- expand XTF coverage beyond the current strong subset
- add compatibility breadth across more packet/channel/vendor variants
- define tested support boundaries instead of implied ones

Why second:

- broad XTF compatibility is a core product capability
- workflow polish should follow actual format coverage, not substitute for it

## Stage 03

`docs/10_plan/STAGE_03_PERFORMANCE_AND_SCALABILITY.md`

Purpose:

- optimize responsiveness and scaling on real survey workloads
- make project open, layer activation, and map interaction feel fast
- turn existing architectural work into workstation-grade performance

Why third:

- the software needs to feel fast on real survey workloads before later workflow polish
- compatibility work will expose real performance pressure that should be addressed early

## Stage 04

`docs/10_plan/STAGE_04_IMPORT_ONCE_WORKFLOW_POLICY.md`

Purpose:

- enforce "parse once" behavior
- reuse existing parsed data
- reopen existing project instead of duplicating import/project state

Why fourth:

- it is a workflow-policy layer on top of the foundation, compatibility, and performance work
- it should follow earlier parser/cache/loading contracts, not redefine them

## Companion Constraint Doc

`docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md`

Use this alongside Stages 01-04.

It is not a fifth stage.

It defines cross-cutting constraints that must be resolved inside the numbered stages:

- source identity and registry direction
- durable parsed-data ownership
- trust and fingerprinting rules
- testing, recovery, and shipped-surface discipline

## Companion Workflow Doc

`docs/10_plan/MULTI_FILE_IMPORT_AND_PARSE_WORKFLOW.md`

Use this whenever work touches:

- batch import
- multi-file loading
- import queueing
- no-project batch project assignment

It is not a separate numbered stage.

It is a mandatory companion spec for Stage 03 and Stage 04 whenever multi-file behavior is in scope.

## Relationship Between The Docs

They are not conflicting.

- Stage 01 is broader and architectural
- Stage 02 is capability-expansion work for XTF compatibility
- Stage 03 is measured speed and scalability work
- Stage 04 is narrower and workflow-specific
- the multi-file workflow doc is a companion spec for Stage 03 and Stage 04, not a separate stage

The cross-cutting doc is a constraint document:

- it does not replace the stage order
- it does affect how each stage should be implemented
- later stages should not contradict its decisions once those decisions are made

The only scope boundary to remember is:

- Stage 04 does not require cache/schema redesign in its first pass
- but if earlier stages introduce better cache/schema/compatibility/loading contracts, Stage 04 should use them
- blocker-level performance issues may still be handled earlier where they are fundamentally architectural

## Implementation Rules

- Do not start a later stage by silently backfilling skipped acceptance criteria from an earlier one.
- Each stage should end with a short closure note listing:
  - what shipped
  - what was tested
  - what intentionally carries forward
- If the cross-cutting doc forces a product-level decision, make that decision explicit before continuing deeper implementation.
