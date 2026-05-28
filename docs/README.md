# Docs README

This is the entry point for the docs system.

If Claude or another implementation agent is starting work, begin here.

## Folder Layout

- `docs/00_control/`
  - policy and execution-control docs
- `docs/10_plan/`
  - active staged implementation docs and cross-cutting constraints
- `docs/20_guides/`
  - working guides for fixtures and hard-problem execution
- `docs/30_templates/`
  - reusable templates for bugs, ideas, and closure notes
- `docs/archive/`
  - completed stage plans and historical closure notes
- `docs/90_closure_notes/`
  - current landing zone for new closure notes before archival

## Required Read Order For Implementation

Read these in order:

1. `docs/README.md`
2. `docs/10_plan/STAGE_00_EXECUTION_ORDER.md`
3. `docs/00_control/DECISION_LOG.md`
4. `docs/00_control/STAGE_GATE_CHECKLIST.md`
5. `docs/00_control/CLAUDE_EXECUTION_BRIEF.md`
6. `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md`
7. the active stage doc in `docs/10_plan/`

## What Each Area Is For

### Control

Use these first:

- `docs/00_control/DECISION_LOG.md`
- `docs/00_control/STAGE_GATE_CHECKLIST.md`
- `docs/00_control/CLAUDE_EXECUTION_BRIEF.md`

This folder answers:

- what product-policy decisions are already locked
- when a stage may start or stop
- how the implementation agent should behave

### Plan

Use these for active staged execution:

- `docs/10_plan/STAGE_00_EXECUTION_ORDER.md`
- `docs/10_plan/STAGE_02_XTF_COMPATIBILITY_EXPANSION.md`
- `docs/10_plan/STAGE_03_PERFORMANCE_AND_SCALABILITY.md`
- `docs/10_plan/STAGE_04_IMPORT_ONCE_WORKFLOW_POLICY.md`
- `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md`
- `docs/10_plan/MULTI_FILE_IMPORT_AND_PARSE_WORKFLOW.md`

Completed stage plans are moved to `docs/archive/`.

### Guides

Use these when the work type needs a process guide:

- `docs/20_guides/XTF_FIXTURE_CATALOG.md`
- `docs/20_guides/HARD_PROBLEM_EXECUTION_PROTOCOL.md`

### Templates

Use these when the work needs a repeatable write-up:

- `docs/30_templates/BUG_INVESTIGATION_TEMPLATE.md`
- `docs/30_templates/IDEA_INTAKE_AND_DECISION_TEMPLATE.md`
- `docs/30_templates/CLOSURE_NOTE_TEMPLATE.md`

### Closure Notes

Write new closure notes here:

- `docs/90_closure_notes/`

Older closure notes may be moved into `docs/archive/`.

### Archive

Use this when you need historical implementation context:

- `docs/archive/10_plan/`
- `docs/archive/90_closure_notes/`

## Quick Routing

If the task is:

- stage implementation:
  - start in `docs/00_control/` then go to `docs/10_plan/`
- multi-file import or batch parsing:
  - use `docs/10_plan/MULTI_FILE_IMPORT_AND_PARSE_WORKFLOW.md`
- parser fixtures:
  - use `docs/20_guides/XTF_FIXTURE_CATALOG.md`
- hard architecture or correctness problem:
  - use `docs/20_guides/HARD_PROBLEM_EXECUTION_PROTOCOL.md`
- deep bug:
  - use `docs/30_templates/BUG_INVESTIGATION_TEMPLATE.md`
- vague new idea:
  - use `docs/30_templates/IDEA_INTAKE_AND_DECISION_TEMPLATE.md`
- slice/stage completion:
  - use `docs/30_templates/CLOSURE_NOTE_TEMPLATE.md`
- historical Stage 01 context or past closure notes:
  - use `docs/archive/`

## Important Note

This file is for navigation and orientation.

For actual precedence and control rules, use:

- `docs/10_plan/STAGE_00_EXECUTION_ORDER.md`
- `docs/00_control/DECISION_LOG.md`
- `docs/00_control/STAGE_GATE_CHECKLIST.md`

## Bottom Line

If you do not know which doc to open next, start here.
