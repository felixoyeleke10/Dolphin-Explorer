# Cross-Cutting Report - Registry, Durability, and Readiness Gaps

Use this together with the control docs.

If this document appears to conflict with a locked decision in `docs/00_control/DECISION_LOG.md`, the decision log wins.

This report covers important work that does not fit cleanly into only one stage.

These are the areas that still look underbuilt if the software is meant to become:

- a serious survey workstation
- a parse-once system
- a company-style source and project catalogue
- a tool users can trust with long-lived survey data

This document does not replace Stages 01-04.

It exists to call out the missing cross-cutting capabilities that affect several stages at once.

## How To Use This Doc

This is a constraint and product-contract document.

It should be used like this:

- not as a separate implementation stage
- not as optional background reading
- but as a set of decisions that must be pulled into Stages 01-04 where relevant

The numbered stage docs still define execution order.
This document defines the bigger contracts those stages should not accidentally violate.

## Highest-Priority Decisions

These are the early decisions most likely to prevent rework:

- what uniquely identifies a dataset:
  - path
  - content hash
  - both
- whether parsed data is:
  - project-owned cache
  - durable shared data product
  - something hybrid with explicit ownership rules
- what the minimum "registry" actually means in product terms:
  - deterministic project lookup only
  - workspace-level source registry
  - broader company catalogue later
- what minimum automated verification is mandatory before compatibility and performance claims are trusted

## Why This Matters

Right now the software has strong architectural potential, but several higher-level product contracts are still incomplete:

- source identity is mostly project-local
- parsed data is still treated like project cache, not durable registered data
- file trust checks are weaker than the model suggests
- testing and recovery are not yet at workstation level
- the UI still exposes more surface area than the backed behavior can support

If these are not addressed, the software can still look polished while remaining fragile in production-style use.

## Current Touchpoints

These are the main code areas connected to these gaps:

- `src/app/Project.h`
  - `ProjectSource`
  - includes `sha256`, `size_bytes`, and `modified_utc_ms`
- `src/app/Project.cpp`
  - `Project::findSourceByPath(...)`
  - `sourceFingerprintMatchesFile(...)`
  - `Project::save()`
  - project-local cache ownership and cache deletion
- `src/app/ImportService.cpp`
  - `inspectSourceFile(...)`
  - `sourceFingerprintMatches(...)`
  - `cachePathForSource(...)`
  - import source registration and parsed-cache reuse
- `src/ui/mainwindow/`
  - project open/import flow
  - Database and other placeholder panels
  - close behavior
- `src/ui/controllers/ImportController.cpp`
  - duplicate-import prompt policy
- `src/ui/widgets/ImportProgressOverlay.cpp`
  - progress display without operator controls
- `src/ui/panels/InspectorPanel.cpp`
  - placeholder contact editing wiring
- `src/ui/shell/Features.h`
  - compile-time feature claims versus shipped behavior

## Missing Work

### 1. There is no real survey or source registry yet

The software has a "Database" concept in the shell, but it is still placeholder-only.

That means imported data is not yet being treated as a globally registered source catalogue entry.

Current behavior is still centered around project-local source lookup:

- `Project::findSourceByPath(...)`
- `ImportService::importFile(...)`

That is enough for one project.

It is not enough for:

- "this file was already imported before"
- "this project already exists elsewhere in the workspace"
- "reuse the parsed product across projects"
- "company database style" source registration

What should exist:

- a source registry layer with stable source identity
- normalized path tracking
- content identity that is stronger than path alone
- registration state independent of one project manifest
- the ability to answer "have we seen this dataset before?" before creating a new project

### 2. Parsed data is still only project-scoped durable data, not a broader durable repository

Today parsed artifacts are written under each project's `data/` folder and tied to the manifest location.

That is visible in:

- `Project::dataPath()`
- `cacheRootForManifest(...)`
- `ImportService::cachePathForSource(...)`

There is also project-owned cache cleanup when a layer is removed.

For the current first-pass staged model, that is acceptable as project-scoped durable data.

It becomes limiting only if the product expects a broader import-once model that works across projects or across a workspace-level registry, for example:

- parse once
- keep parsed output
- do not re-import if the source was already registered
- only rebuild when the source changed or is truly unavailable

What would be needed for that broader model:

- a durable parsed-artifact repository
- ownership separated from one project manifest
- a distinction between:
  - temporary render cache
  - parsed canonical data product
  - project-specific derived products

Until then, "import once" remains a workflow policy layered on top of project-owned parsed artifacts, not a true shared artifact service.

### 3. Source trust and identity are weaker than they should be

`ProjectSource` already contains `sha256`, but the live fingerprint checks currently rely on:

- file size
- modified time

That means the data model is stronger than the actual trust model.

For durable parsed data, that is risky.

Examples of failure modes:

- source file contents change while size and timestamp appear unchanged
- copied files from different locations are treated as unrelated even when content is identical
- same basename or renamed source complicates project reuse logic

What should exist:

- a real source identity contract
- content hash support used in actual decision-making
- staged trust rules if full hashing is too expensive on first pass
- a clear distinction between:
  - path identity
  - content identity
  - project membership

### 4. There is no automated verification foundation yet

The top-level CMake build does not define test execution at all.

That is a major readiness gap for software that wants to support:

- broad XTF compatibility
- raw/cache parity
- import-once behavior
- project reopen consistency
- performance claims on large datasets

What should exist:

- parser fixture tests
- malformed-file tests
- cache/raw parity tests
- stale-cache invalidation tests
- project reopen and migration tests
- benchmark or repeatable perf baselines

Without this, the software will keep depending on manual confidence and memory.

### 5. Dirty-state, autosave, and crash-recovery are still weak

Project manifest writes use `QSaveFile`, which is good.

But atomic save is not the same thing as recovery.

Right now the window close path mainly saves geometry, and I do not see a stronger unsaved-changes or autosave contract around normal project work.

The project emits `modified()`, but it is not being used as part of a visible save/recovery story.

What should exist:

- unsaved-state tracking surfaced in the shell
- autosave or background manifest save policy
- recovery behavior after interrupted import or abrupt close
- clear handling for in-flight work when the app exits

This matters more once projects hold long-running imports, contacts, graphs, and durable parsed data references.

### 6. Import jobs have progress UI but almost no operator control

The import overlay is visually polished, but it is still passive.

It can:

- show progress
- show success
- show failure

It cannot really:

- cancel a large import
- pause or defer work
- resume intentionally
- explain what stage the import is in

For large survey files, progress without control is not enough.

What should exist:

- cancelable import jobs
- explicit phase reporting
  - opening
  - scanning
  - indexing
  - cache writing
  - finalize/save
- better behavior if a user closes the project or app mid-import

### 7. The shipped surface still overpromises

There are still too many visible placeholders and Phase 2 actions for a tool aiming at serious operator trust.

This includes:

- Database panel
- Processing panel shell
- Analyze / AI / Present / Report placeholder pages
- export menu actions
- reindex action stub
- contact inspector placeholder wiring

The issue is not just "unfinished features exist."

The real issue is that the UI suggests capabilities that are not yet implemented to workstation depth.

What should exist:

- a stricter rule for what is allowed on the shipped surface
- hidden, disabled, or internal-only actions for unfinished workflows
- better distinction between:
  - implemented
  - partial
  - planned

### 8. Diagnostics and operational observability are still too thin

There are hints of good internal structure for execution summaries, especially in the pipeline graph job model.

But I do not see a mature operator-facing diagnostics story yet.

That becomes important when users need to understand:

- why an import failed
- whether a source was reused or rebuilt
- why a cache was invalidated
- which XTF variant was recognized or rejected
- where time is being spent during large operations

What should exist:

- structured import and parse diagnostics
- cache decision logging
- clearer unsupported-format reporting
- visible execution summaries for expensive operations
- a foundation for future benchmark and support workflows

## Recommended Execution Guidance

These gaps should influence the stage work without replacing it.

### Before or during Stage 01

- define the longer-term source identity model
- decide whether parsed artifacts are project cache or durable registered data
- define what "Database" actually means in the product

### During Stage 02

- expand XTF support together with explicit compatibility reporting
- build fixture coverage instead of relying on ad hoc manual checks
- ensure broader format support plugs into the same source-identity contract

### During Stage 03

- add measured performance baselines
- expose better job diagnostics
- make long-running work controllable, not just visible

### During Stage 04

- implement import-once policy on top of the real registry and artifact rules
- do not let Stage 04 become a UI-only workaround for missing source identity

## Implementation Readiness Rule

If any team member can read a stage doc and still reasonably ask:

- "What is the source identity?"
- "Who owns parsed artifacts?"
- "Can this data be deleted when a layer goes away?"
- "How do we know a source is really the same?"

then this cross-cutting track is not resolved enough yet.

## Acceptance Criteria For This Cross-Cutting Track

This report can be considered addressed when the software can honestly support statements like these:

- "This dataset is registered once and recognized everywhere it should be."
- "Parsed output survives project-level workflow changes unless intentionally deleted."
- "The app can prove why it reused, rebuilt, or rejected a source."
- "Regression confidence does not depend only on manual testing."
- "Long-running import work is observable and controllable."
- "The visible UI surface mostly represents real implemented capability."

## Bottom Line

The biggest under-noticed gap is not a single parser bug or UI stub.

It is that the software still lacks a fully defined contract for:

- what a source is
- where parsed data lives
- how trust is established
- how recovery works
- what the product is willing to promise in the UI

That contract is what will separate a promising application from a dependable survey platform.
