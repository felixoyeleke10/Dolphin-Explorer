# Stage 04 - Import Once Workflow Policy

This report is for implementing a stricter import workflow:

- parse a source file once
- reuse parsed data on later imports
- do not create duplicate projects for the same source
- only rebuild when the source changed, the source is missing/restored, or the parsed cache is stale/invalid

The goal is to make import behave more like a database registration step than a repeated ad hoc parse action.

This is a stage-4 workflow document.

It builds on earlier work, especially:

- stable source and cache validity rules
- better import/cache contracts
- clearer ownership of import/open behavior in the UI shell
- broader XTF compatibility coverage from Stage 02

Important scope note:

- this document is written as a workflow-policy pass first
- it does not require parser/cache/schema redesign for its first implementation pass
- however, if Stage 01 or Stage 02 introduces deeper cache, metadata, or compatibility changes, this stage should follow those contracts rather than override them

## Entry Criteria

Stage 04 should not be implemented as a workaround for missing earlier decisions.

Before starting:

- Stage 01 import/cache/source contracts should be stable enough to depend on
- Stage 03 responsiveness should already be good enough that reuse-first behavior feels fast, not sticky
- if the cross-cutting registry/durability report introduced a real source-registry direction, this stage should use it instead of inventing a parallel rule set

## Goal

When a user imports survey data:

- the source should be registered once
- parsed output should be reused on later attempts
- the app should tell the user the data already exists instead of importing it again
- the app should reopen the existing project when appropriate instead of creating another one

This is especially important for XTF, but the workflow should be format-agnostic.

## Current Touchpoints

These are the main places that currently control the workflow:

- `src/ui/mainwindow/MainWindow.Logic.cpp`
  - `MainWindow::onImportFile(...)`
  - `MainWindow::showImportDialog(...)`
  - when no project is open, it currently asks the user to save a new project or continue into an app-local `Session_*` temp project
- `src/ui/controllers/ImportController.cpp`
  - `ImportController::importBatch(...)`
  - handles duplicate source detection inside the current project
- `src/app/ImportService.cpp`
  - `ImportService::importFile(...)`
  - resolves source registration, layer creation, cache path, and background parsing
  - `buildArtifactStore(...)`
  - reuses parsed cache when source fingerprint matches
- `src/app/Project.cpp`
  - `Project::findSourceByPath(...)`
  - source uniqueness within a project
  - `fromJson(...)`
  - invalidates stale cache/index state on project open

## Current Problems

### 1. Duplicate import is still treated as a normal user choice

Inside an existing project, duplicate detection already exists in `ImportController::importFile(...)`, but the UI still offers:

- use existing
- rebuild
- import another layer

That makes duplicate import feel normal instead of exceptional.

For the workflow you want, the default stance should be:

- same source path means same registered dataset
- do not import again unless there is a very specific reason

### 2. No-project import still creates a fresh project decision before existing-project lookup

`MainWindow::importFile(...)` currently makes a new-project decision when no project is open:

- either the user chooses a brand-new named project
- or the app creates an app-local `Session_*` temp project

That means the app does not first ask:

- does a project for this source already exist?
- does parsed data already exist?

So the same survey can end up looking like a new import session instead of reopening the existing project.

### 3. Project identity is still creation-flow-driven, not source-driven

The current no-project path is driven by whichever creation path happens first:

- a user-selected new project name/path
- or an app-local `Session_*` temp project name

That is workable for storage, but it is not a strong identity rule for import-once behavior.

Risks:

- duplicated project folders for the same actual source
- user confusion about whether the data is already "in the database"
- temp-session imports that feel like new work instead of reopening known work

### 4. The workflow is not strict enough about "registered once"

The code already has most of the pieces:

- source path lookup
- source fingerprint checks
- parsed cache validation
- stale cache invalidation on project open

But those checks are not yet enforced as a single import policy.

## Desired Behavior

## Rule 1: Same source path in the same project means already imported

If the user imports a file whose normalized path already exists in the current project:

- do not create a new source
- do not create a new layer by default
- do not start a new parse by default

Instead:

- if parsed cache is valid and the layer is indexed:
  - show `Data already imported`
  - activate/select the existing layer
- if the source exists but cache/index is stale or missing:
  - rebuild the existing layer
  - do not create a duplicate layer
- if the source is registered but the source file is missing:
  - show `Source file missing`
  - do not create a duplicate layer

## Rule 2: No open project should still reuse an existing project

If the user imports a file while no project is open:

- first determine whether a project for that source already exists
- if it exists, open that project
- then apply the same duplicate-data rules as above

This rule should run before either of these current fallback paths:

- creating a new named project
- creating an app-local `Session_*` temp project

Expected user-facing behavior:

- `Project already exists`
- `Data already imported`

Not:

- silent new project creation
- silent duplicate import

## Rule 3: Parse once unless invalidated

Parsed data should only be regenerated when one of these is true:

- the source file changed
- the source file was deleted and later restored
- the parsed cache is missing
- the parsed cache version is stale
- the user explicitly requests rebuild/reindex

Everything else should reuse the existing parsed data.

## Rule 4: "Import Another Layer" should not exist for the same exact source unless justified

If there is no strong product reason to allow multiple layers from the exact same raw file, remove that option.

If there is a real use case for multiple logical layers from one source, it should be explicit and named accordingly, for example:

- `Create Derived View`
- `Create Processing Variant`

Not:

- `Import Another Layer`

because that reads like a duplicate registration path.

## Recommended Implementation Direction

## 1. Create an explicit import decision flow

Before any new project/source/layer creation, compute:

- normalized source path
- current project state
- existing project candidate
- existing source candidate
- existing layer candidate
- source fingerprint validity
- parsed cache validity

From that, branch into one of a small number of outcomes:

- open existing project and activate existing layer
- reuse existing parsed data in current project
- rebuild existing layer
- report missing source
- create brand-new project and import

That decision should happen before side effects.

## 2. Treat source registration as unique

Within a project:

- normalized source path should be unique

Across auto-created projects:

- the app should have a deterministic way to find the existing project for a source before making a new one

Possible approaches:

- current project-folder convention plus manifest existence check
- a lightweight registry/index of known project manifests
- project metadata that stores source-path ownership

The simplest first step is probably:

- keep the existing project-folder convention
- check for existing manifest first
- open it before doing anything else

For the current staged plan, that lookup should stay inside the app-managed local project area defined in `docs/00_control/DECISION_LOG.md`.

## 3. Reuse existing layers by default

When duplicate source path is detected, choose one preferred existing layer:

- first indexed layer with valid parsed cache
- else first indexed layer
- else first layer linked to that source

Then:

- activate that layer if usable
- rebuild that layer if stale
- never silently create another one

## 4. Keep rebuild explicit but automatic when required

If the source is registered and the raw file still exists, but cache/index is stale:

- rebuild automatically against the existing layer
- tell the user why

Suggested messages:

- `Data already registered. Parsed data is out of date, rebuilding now.`
- `Data already registered. Using existing parsed data.`
- `Project already exists. Opened existing project.`

## Acceptance Criteria

The implementation should satisfy all of these:

- Importing the same file twice in the same project never creates a duplicate parsed copy.
- Importing the same file twice in the same project never creates a duplicate layer unless a future explicit derived-layer feature intentionally does so.
- Importing the same file with no project open reopens the existing project instead of creating a new one.
- A valid parsed cache is reused without reparsing.
- A stale cache rebuilds the existing layer instead of creating a new one.
- A missing source file produces a clear message and does not create a duplicate registration.
- User messaging clearly says whether the app reused data, reopened a project, or rebuilt parsed data.

## Concrete Deliverables

By the end of Stage 04, implementation should include:

- one explicit import decision flow or state machine
- one preferred existing-layer selection rule
- one existing-project lookup path before new-project creation
- user-visible reuse/reopen/rebuild messaging
- regression coverage for duplicate import and reopen scenarios

## Edge Cases To Test

- same XTF imported twice in one session
- same XTF imported after app restart
- same XTF imported with no project open when project folder already exists
- same XTF with valid cache
- same XTF with stale cache version
- same XTF with modified raw file timestamp/size
- same XTF with missing raw file
- two different files with the same basename in different folders
- project manifest exists but contains no valid indexed layer for that source

## Suggested Message Copy

### Existing parsed data

`Data already imported. Using the existing parsed data.`

### Existing project

`Project already exists. Opened the existing project instead of importing again.`

### Rebuild required

`Data already registered, but parsed data is out of date. Rebuilding now.`

### Missing source

`This data is already registered, but the source file is missing. Restore the source file before rebuilding.`

## Non-Goals For This Stage

This report does not propose, as part of its first pass:

- changing the parser itself
- redesigning cache format
- redesigning project manifest schema

- defining a completely new registry or durable-artifact model if earlier stages have not chosen one yet

This is a workflow/policy change first.
If earlier stages establish new cache, metadata, compatibility, or performance-loading contracts, Stage 04 should use them.

## Scope Boundary

Stage 04 can deliver a strong first pass at the project/workspace level.

But if the product wants true company-wide or multi-project source registration, that depends on the cross-cutting registry and durability work being real, not implied.

## Bottom Line

The app already has most of the technical pieces needed for this behavior.

What is missing is a strict import policy:

- same source path should map to one registered dataset
- same dataset should reuse parsed data
- same survey should reopen the existing project

That policy should be enforced before any new project, source, layer, or parse job is created.
