# Multi-File Import And Parse Workflow

This document defines the expected behavior for importing and parsing more than one survey file in one user action or one user session.

It exists because the current software has some multi-file behavior already, but not yet a clear multi-file product contract.

Right now:

- drag-and-drop can feed multiple files into import one by one
- the import service can run background jobs asynchronously
- the progress overlay can display multiple jobs

But that is not the same thing as having a real multi-file workflow.

## Why This Needs Its Own Doc

Multi-file import is not only a parser issue.

It also affects:

- project assignment
- duplicate detection
- batch-level user messaging
- queueing and bounded concurrency
- responsiveness during heavy ingest
- partial failure behavior

If this is left implicit, an implementation agent can easily build something that "works" while still making the wrong workflow decision.

## Current Behavior

The current code already shows partial support:

- drag-and-drop loops over multiple incoming URLs
- each matching survey file is passed to `importFile(...)`
- each import runs asynchronously
- each job can appear in the progress overlay

But the current product contract is still weak:

- the file-picker import action now allows multi-select, but it still falls back to repeated single-file import calls
- import APIs are singular, not batch-oriented
- no project-level batching policy exists
- no bounded concurrency or explicit queue policy exists
- no batch-level summary exists
- no batch dedupe/reuse policy exists

## Primary Problem To Solve

The real question is not:

- "can the code parse several files?"

The real question is:

- "when a user brings several files at once, what should the software do at the project, queue, and messaging levels?"

That behavior must be explicit.

## Desired Behavior

The multi-file workflow should support three main user intents:

### 1. Add Several Related Survey Files To The Current Project

Example:

- the user already has a project open
- they select or drop several XTF or JSF files that belong to the same survey workspace

Expected behavior:

- each file is evaluated against import-once rules
- existing registered files are reused or rebuilt, not duplicated
- new files become new registered sources in the current project
- the app shows per-file progress and a batch-level summary

### 2. Start From Several Survey Files With No Project Open

Example:

- the user selects or drops several files before any project is open

Expected behavior:

- the app should not silently make project decisions file by file
- it should resolve a project-assignment policy first

First-pass acceptable behavior:

- create or open one project only when the selected files are being treated as one batch import into one workspace
- do not create multiple surprise projects in one batch action

If the selected files clearly belong to different project targets, the workflow should stop and ask rather than guess.

For the first-pass staged implementation, "ambiguous" has a strict meaning defined by `docs/00_control/DECISION_LOG.md`:

- more than one unique existing project target is found across the batch
- or no existing project target is found and the files do not all share one parent directory

### 3. Re-Run A Batch That Includes Existing Data

Example:

- some files are already imported
- some are stale
- some are new
- some are missing or invalid

Expected behavior:

- existing valid files are reused
- stale files rebuild their existing layer/source path
- missing files report clearly
- new files import normally
- the batch finishes with a summary of:
  - reused
  - rebuilt
  - newly imported
  - failed

## Required Product Rules

### Rule 1: Batch Intent Must Be Decided Before Side Effects

Before any new project, source, or layer creation starts, the app should determine:

- is there a current project?
- how many survey files were supplied?
- are they being imported into one current project?
- should an existing project be reopened?
- is the batch ambiguous enough that user confirmation is required?

The app should not decide those separately per file after work already started.

### Rule 2: Import-Once Rules Still Apply Per File

Multi-file import does not weaken the import-once policy.

For each file in the batch:

- same registered source should reuse or rebuild
- same source should not silently create duplicate layers
- same project should not get duplicate source registrations

Batch behavior sits on top of per-file identity rules, not instead of them.

### Rule 3: Queueing Must Be Explicit

The app should define whether batch imports are:

- serial
- bounded parallel
- unbounded parallel

For this product, the locked first-pass answer is:

- bounded parallel with visible queue state
- default heavy-work concurrency limit: `2`

The app should avoid:

- launching an uncontrolled number of heavy parses at once
- making UI responsiveness depend on how many files the user dropped

### Rule 4: Project Assignment Must Be Predictable

If a project is already open:

- batch import targets that project

If no project is open:

- the app must resolve project assignment before importing

First-pass acceptable policy:

- one batch action maps to one chosen project target
- if that target already exists, open it
- if it does not, create it
- if the batch is ambiguous, stop and ask

The exact first-pass ambiguity rule is locked in `docs/00_control/DECISION_LOG.md` under `D-11`.

### Rule 5: The User Needs Batch-Level Feedback

Per-file cards are good, but not enough.

The workflow should also provide:

- total files in batch
- current queue state
- how many reused
- how many rebuilt
- how many newly imported
- how many failed

The user should not have to mentally reconstruct the batch outcome from scattered per-file messages.

## Recommended First-Pass Scope

To keep this implementable, the first pass should focus on:

- multi-select file import
- controlled queueing
- one-project batch target behavior
- per-file import-once decisions
- batch completion summary

The first pass does not need:

- arbitrary multi-project batch routing
- a full company-wide registry
- perfect automatic grouping of unrelated survey sets

## Recommended Execution Placement

This workflow depends on staged work already in the plan.

### Stage 01 Dependencies

Needs:

- stable import/cache/source contracts
- predictable shell behavior
- safer activation/loading behavior

### Stage 03 Dependencies

Needs:

- queueing and responsiveness discipline
- bounded background work
- cancellation or interruption policy
- the first-pass heavy-work concurrency limit from `D-14`

### Stage 04 Dependencies

Needs:

- import-once per-file policy
- existing-project lookup behavior
- reuse/rebuild/new classification rules
- the first-pass batch project-assignment rule from `D-11`

## Implementation Direction

### 1. Add A Batch-Oriented Entry Path

The app should have a dedicated multi-file import entry path instead of pretending repeated single-file calls are the same thing.

That path should:

- accept a list of files
- normalize and classify them first
- decide the project target first
- enqueue per-file actions after that

### 2. Introduce A Batch Import Coordinator

Conceptually, the workflow should have a coordinator object or equivalent logic that owns:

- batch id
- file list
- project target
- queue state
- per-file decision result
- final summary

This keeps batch policy out of ad hoc `for` loops in UI code.

### 3. Separate Per-File Decision From Per-File Execution

For each file, compute one of:

- reuse existing
- rebuild existing
- import new
- fail missing/invalid

That decision should happen before heavy parse work starts.

### 4. Bound Concurrency

Use a small explicit concurrency limit.

The first-pass locked default is:

- `2` simultaneous heavy import/decode jobs

That number can be tuned later only after Stage 03 benchmark evidence justifies a change.

The important part is:

- not unlimited
- visible to the user
- cancelable if possible

### 5. Add A Batch Summary Model

At minimum, batch completion should be able to report:

- total files submitted
- total reused
- total rebuilt
- total imported new
- total failed

Suggested first-pass message:

- `Batch import complete: 7 files processed, 3 reused, 2 rebuilt, 2 newly imported, 0 failed.`

## Edge Cases To Handle

- multiple files dropped with no project open
- same file repeated twice in one batch
- mixture of new, existing, stale, and missing files
- two different files with the same basename
- batch includes a file already being indexed
- one file fails while others succeed
- user closes project or app while batch is in progress
- batch contains unsupported but recognized formats

## Acceptance Criteria

The multi-file workflow should be considered ready for first-pass use when:

- the user can select or drop multiple survey files in one action
- the app does not silently create duplicate layers or duplicate project state during batch import
- per-file import-once rules still hold inside the batch
- queueing is bounded and visible
- the UI remains responsive during batch import
- the user gets both per-file progress and a final batch summary
- ambiguous no-project batch cases do not get silently guessed
- the no-project batch ambiguity rule matches `D-11`
- the default concurrency limit matches `D-14` unless later benchmark evidence explicitly replaces it

## Non-Goals For The First Pass

- automatic intelligent grouping across many unrelated surveys
- multi-project batch routing in one action
- networked registry integration
- perfect scheduling optimization

## Bottom Line

Multi-file import should be treated as a product workflow, not just a loop over `importFile(...)`.

The first-pass goal is:

- predictable project assignment
- correct per-file reuse/rebuild/new behavior
- bounded background execution
- clear batch feedback

That is enough to make multi-file loading and parsing feel deliberate instead of accidental.
