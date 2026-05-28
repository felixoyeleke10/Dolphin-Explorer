# Decision Log

This file exists to remove product-policy ambiguity before implementation begins.

Use it like this:

- if a decision is marked `Locked`, the implementation agent should follow it
- if a decision is marked `Provisional`, the implementation agent may use it only for the current stage and should not widen its scope
- if a decision is marked `Open`, the implementation agent should stop and ask instead of inventing a policy

The goal is simple:

- humans choose product policy
- the implementation agent executes within that policy

## Current Working Status

The decisions below are the working defaults for Stages 01-04 unless explicitly changed here.

## D-01: Current Registry Scope

Status: `Locked`

Decision:

- the current implementation target is local workspace scope, not a networked or company-wide service
- the software should be able to answer:
  - "have we already seen this source in this workspace?"
  - "does a project for this source already exist here?"

For the current staged plan, "local workspace scope" means:

- the app-managed local project area used by Dolphin Explorer for deterministic project creation and lookup
- not an arbitrary crawl of the whole machine
- not a network location
- not a shared multi-user service

Not part of the current implementation target:

- multi-user shared service
- network registry
- central company database server

Why:

- this keeps the stages implementable
- it matches the current codebase shape
- it avoids forcing Stage 04 to wait for infrastructure that does not exist yet

## D-02: Dataset Identity For Stages 01-04

Status: `Locked`

Decision:

- the first-pass dataset identity is:
  - normalized absolute source path within the current workspace
  - plus file fingerprint validation using size and modified time

`sha256` is treated as future hardening, not as a blocker for current staged implementation.

Why:

- it aligns with the current project model and import flow
- it is strong enough to implement deterministic reuse and reopen behavior now
- it avoids blocking the staged plan on a deeper hashing rollout

Important note:

- if a later hardening pass upgrades source identity to content-hash-first, that should be done explicitly and should update this file

## D-03: Existing Project Lookup Rule

Status: `Locked`

Decision:

- before creating a new project from an imported source, the app must check for an existing project in the local workspace
- the first-pass lookup may use the existing deterministic local project convention
- if a stronger workspace registry is introduced later, it should replace the convention-based lookup rather than sit beside it
- the first-pass implementation should search only the app-managed local project area defined in `D-01`

Why:

- this supports the import-once workflow without needing a full new backend
- it prevents silent duplicate project creation from becoming "normal"

## D-04: Parsed Artifact Ownership

Status: `Locked`

Decision:

- for the current staged implementation, parsed artifacts are durable workflow assets, not disposable render cache
- however, they may remain project-scoped in storage for the first pass
- Stage 04 should not promise true cross-project shared parsed-artifact reuse unless that capability is explicitly implemented

What this means:

- do not treat the project-owned parsed data store (`.dlpd`; legacy `.dpcache` accepted on read) as throwaway whenever a UI layer changes
- do not claim a global shared parsed-data repository unless one actually exists
- do support reliable reopen and reuse within the chosen workspace/project model

Why:

- this avoids promising more than the implementation really supports
- it still improves trust and reuse behavior immediately

## D-05: Shipped Surface Policy

Status: `Locked`

Decision:

- unfinished workstation features should be hidden, disabled, or clearly marked as unavailable
- clickable "Phase 2" actions should not remain on the shipped surface as if they are working features

Why:

- trust matters as much as capability
- false surface area creates confusion faster than missing menu items

## D-06: Loading Policy

Status: `Locked`

Decision:

- the app should prefer:
  - index/extents first
  - visible-first decode
  - background refinement second

The app should avoid:

- full-line decode as a requirement for simple layer activation
- eager activation of every already-indexed heavy layer on project open

Why:

- this supports both Stage 01 architecture fixes and Stage 03 performance goals

## D-07: Stage Discipline

Status: `Locked`

Decision:

- the implementation agent should finish one stage or slice at a time
- it should not silently start "borrowing" work from the next stage under the label of cleanup

Why:

- stage bleed is where AI work becomes hard to review
- this keeps acceptance criteria meaningful

## D-08: Verification Floor

Status: `Locked`

Decision:

- parser and import-contract changes require regression coverage
- performance claims require repeatable benchmark evidence
- stage closure requires a short written note of what changed and what was tested

Why:

- this project is moving beyond manual-confidence-only territory

## D-09: When The Agent Must Stop

Status: `Locked`

Decision:

The implementation agent must stop and ask for direction if the current work would require choosing any of these without guidance:

- path identity versus content-hash identity beyond the first-pass rule above
- true cross-project shared parsed-artifact ownership
- workspace-local registry versus broader network/company registry
- user-facing behavior that contradicts the shipped-surface policy

## D-10: Control Docs Are Not Self-Editable Escape Hatches

Status: `Locked`

Decision:

- the implementation agent must not rewrite `docs/00_control/DECISION_LOG.md`, `docs/00_control/STAGE_GATE_CHECKLIST.md`, or `docs/00_control/CLAUDE_EXECUTION_BRIEF.md` just to unblock code work
- changes to those control docs should happen only when a human explicitly requests a policy or process update

Why:

- otherwise the agent can "solve" blockers by redefining the rules instead of implementing within them
- this keeps review trustworthy

## D-11: Multi-File Batch Project Assignment

Status: `Locked`

Decision:

For the current first-pass staged implementation:

- if a project is already open, a multi-file batch targets that project
- if no project is open, resolve existing project targets for all files first using `D-03`
- if exactly one unique existing project target is found across the whole batch:
  - open that project
  - route the whole batch there
- if no existing project targets are found and all files share the same parent directory:
  - create one new batch target project
  - route the whole batch there
- otherwise the batch is ambiguous and must stop and ask

Also:

- repeated identical source paths inside one batch should be deduplicated before side effects

Why:

- this gives a deterministic first-pass rule
- it prevents file-by-file surprise project creation
- it is strict enough to be safe without requiring a full registry backend

## D-12: Review Horizon For First-Pass Policy

Status: `Locked`

Decision:

- the first-pass policy decisions in `D-01` through `D-04` and `D-11` remain in force through Stage 04 closure
- the cross-cutting doc may identify better long-term architecture, but it does not reopen these locked first-pass decisions during active stage work
- revisiting those decisions requires:
  - a human-directed policy update
  - and an explicit update to this file

Why:

- this prevents the implementation agent from drifting between "current policy" and "future ideal"
- it keeps staged execution reviewable

## D-13: Test Harness Standard

Status: `Locked`

Decision:

- the first real automated harness must live under `tests/`
- it must be integrated into the top-level build through CMake/CTest
- the default validation entry point should be `ctest --output-on-failure`
- parser or import fixtures should live under `tests/fixtures/` when they can be checked in
- if fixtures cannot be checked in, the external fixture manifest must still be documented in `docs/20_guides/XTF_FIXTURE_CATALOG.md`
- ad hoc scripts alone do not satisfy staged test-gate requirements

Why:

- this prevents throwaway one-off harnesses
- it gives Stage 02 and later stages a stable test foundation

## D-14: First-Pass Heavy-Work Concurrency Limit

Status: `Locked`

Decision:

- until Stage 03 benchmark evidence justifies a different value, the first-pass default limit for simultaneous heavy import/decode jobs is `2`
- heavy work beyond that limit should queue visibly rather than launch unbounded background tasks

Why:

- this gives the multi-file workflow a concrete first-pass queueing rule
- it prioritizes responsiveness over uncontrolled background throughput

## Change Control

When a decision changes:

- update this file first
- keep the old decision in version control history
- do not let the implementation agent "discover" a new product policy only from code changes

## Bottom Line

This file exists so the implementation agent does not have to guess what kind of product it is building.

For the current staged plan:

- local workspace scope is enough
- normalized path plus current file fingerprinting is the first-pass identity rule
- parsed artifacts are durable workflow assets, but not yet a claimed global shared repository
- unfinished surface area should not be shipped as if it works
- multi-file no-project batch assignment follows one deterministic first-pass rule
- automated stage validation is expected to grow on top of one real `tests/` + CTest entry point
