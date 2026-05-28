# Stage 01 - Foundation Remediation for MainWindow and XTF Workflow

This document defines the foundation work required to make the `MainWindow` and XTF parsing workflow feel production-ready instead of merely functional.

This is the foundation-stage document.

It covers:

- `MainWindow` state ownership and shell architecture
- XTF/import/cache consistency
- metadata persistence contracts
- progressive loading direction for large survey lines

Later workflow documents should assume this stage is the architectural baseline.

## Entry Criteria

Before implementation starts, treat this as contract work, not just cleanup work:

- `MainWindow` shell changes are allowed to move behavior out of the class
- import/cache/metadata work is allowed to change cross-module contracts
- the cross-cutting registry/durability report in `docs/10_plan/CROSSCUTTING_REGISTRY_DURABILITY_AND_READINESS.md` should be used to settle any source-identity or parsed-data ownership decisions that would otherwise force rework later

## Executive Summary

The project has strong structure and real product intent, but two areas now need explicit boundary-setting:

- `MainWindow` became both the UI shell and the feature policy layer.
- The XTF import path became capable before it became easy to reason about and verify.

Neither area is "bad". Both are close to solid. The issue is that their boundaries, tests, and failure handling now need to catch up with their importance.

## Critical Findings From a Deeper Pass

These are not just stylistic concerns. They are concrete design and behavior problems that should be addressed.

### MainWindow

#### 1. The workspace UI has multiple state paths that do not agree

The window has two different ways to open panels:

- `togglePanel(...)`
- `onActivityPanel(...)`

Only `togglePanel(...)` updates:

- activity button check states
- animated panel geometry
- layer picker repositioning

`onActivityPanel(...)` only flips the stacked widget page and visibility. That means menu-driven panel changes and contact-selection-driven panel changes do not fully update the shell state.

This is the classic sign of shell drift: one UI action path is "real", the other is "almost real".

#### 2. Workspace customization is internally broken

The right toolbar customization path is not just incomplete. It is currently inconsistent:

- `m_right_tool_bar` exists as member state
- the shell builds a right toolbar widget
- but that returned widget is not assigned back to `m_right_tool_bar`

As a result, the custom workspace code that tries to show/hide the right toolbar is operating on a null member and silently doing nothing.

The properties panel path has a similar issue:

- customization code calls `setVisible(...)` directly
- animation/state code relies on `m_props_open`

So the UI can become visibly open while the internal state still says "closed". Once that happens, resize logic, animation, and toggle behavior are no longer trustworthy.

This should have been designed around a single workspace state model, not direct widget visibility edits scattered across the window.

#### 3. The status bar mixes two unrelated responsibilities

`m_status_line` is being used for both:

- persistent context display
- transient job/status messages

That creates self-overwriting behavior:

- `appendJobMessage(...)` writes a message
- `updateContextInfo()` writes over it
- selection handlers often do both in sequence

So the user gets flashes of feedback instead of stable state. A production shell should have had separate channels:

- persistent context
- transient notifications

#### 4. Feature flags are advertised as independent, but the shell does not honor that contract

The feature flag comments imply clean compile-time exclusion, but the window and controller wiring are not actually isolated that way.

Examples:

- import menu/toolbar actions are still created even though the import controller is only conditionally created
- `SidescanViewController` is always constructed, even though its import dependency is feature-gated
- the processing workspace is tied to `kNodeGraph` in some places and `kProcessing` in others
- contacts access is partially guarded and partially assumed

That means the flags are not true module boundaries. They are partial conditionals over a still-coupled shell.

#### 5. MainWindow is acting as both shell and behavior policy

This showed up more strongly on the deeper pass than on the first pass.

The class is not only composing widgets. It is also deciding:

- which actions are enabled
- how modality selection affects the map
- how panel/workspace state should behave
- which feature branches should be reachable
- what import/open/save flows should do after completion

That is exactly how a good shell gradually turns into a maintenance hotspot.

### XTF / Import Workflow

#### 6. Cache-hit imports still lose metadata

This is an important one.

When `buildArtifactStore(...)` reuses an existing parsed cache, it returns early after building the cache-backed index. But on that path it does not populate:

- survey name
- vessel name
- start/end time
- frequency

The cache format itself also does not persist that metadata as first-class file metadata.

So a valid cache hit can regress layer metadata even though the data is otherwise usable. That is the kind of bug that makes software feel flaky even when parsing "worked".

#### 7. The cache metadata path is also ordered incorrectly

On the cache-hit path, metadata is read before `ParsedCacheReader::buildIndex(...)` populates the derived fields such as time span and artifact counts.

So even the metadata that the cache reader *can* derive is not being collected at the right time.

This should have been an explicit import result contract, not an incidental side effect of reader state.

#### 8. Layer activation still decodes the entire sidescan line on the UI thread

This is one of the more serious workflow issues.

There is a windowed read API:

- `loadSidescanWindow(...)`

But the activation path currently uses:

- `loadAllSidescanPings(...)`

That means selecting a sidescan layer decodes the full line synchronously in the UI/controller path before the map is updated.

That creates three scaling risks:

- UI stalls on large XTF lines
- high memory pressure from loading full decoded ping sets
- slow project open because already-indexed sidescan layers are auto-loaded up front

This should have been progressive:

- lightweight index-first activation
- viewport/window-driven decode
- background loading for full-resolution swaths

#### 9. The map/status presentation still confuses coordinate spaces

There are still places where display coordinates are treated like geographic coordinates:

- projected or pseudo-geographic map coordinates are labeled as `Lat/Lon`
- cursor/status text assumes degree semantics
- track distance in the sidescan controller is computed with a fixed `111320 m/deg` conversion even after map normalization

That means the app can show believable-looking numbers that are semantically wrong. This is more dangerous than an obvious failure.

#### 10. Startup and selection are doing too much eager work

Project load auto-activates every indexed sidescan layer onto the map. Each activation performs full decode and map insertion work.

That means open-time cost scales with:

- number of indexed sidescan layers
- size of each line
- full swath reconstruction cost

A more robust design would have loaded:

- nav extents immediately
- visible swath data lazily
- off-screen or inactive data on demand only

## Stage 01 Requirements for MainWindow

### 1. Keep `MainWindow` as an orchestrator only

`MainWindow` should have owned:

- application shell composition
- top-level navigation
- controller wiring
- high-level project lifecycle events

It should not have accumulated:

- feature-specific action policy
- placeholder business actions
- direct modality branching
- growing chunks of view-state coordination

Better shape:

- `MainWindow`
  - window shell
  - menus/toolbars/status shell
  - project open/save/import routing
- `WorkspaceController`
  - active layer selection
  - action enable/disable policy
  - map/inspector synchronization
  - panel/workspace state transitions
- `ExportController`
  - export availability
  - export commands
- `ProjectUiBinder`
  - bind/unbind project to widgets

### 2. Move action-state logic behind a dedicated policy object

`updateActionStates()` is the right instinct, but it should have existed much earlier and lived outside the window class.

What should have existed:

- a single action policy model driven by:
  - project loaded
  - active layer
  - indexed data available
  - current modality
- one place to compute UI capability
- one place to bind that capability to toolbar and menu actions

That avoids drift between toolbar buttons, menu items, and context-specific actions.

The same rule should have applied to workspace visibility:

- one source of truth for panel visibility
- one source of truth for active workspace/panel
- no direct `setVisible(...)` calls that bypass shell state

### 3. Remove or hide stubs from the shipped surface

Phase-2 actions should not have been exposed as if they were working features.

Better options:

- hide unfinished actions behind compile-time flags
- keep them disabled with a clear tooltip
- place them in an internal/dev menu only

Showing clickable items that only emit `"Phase 2"` lowers trust faster than simply omitting them.

### 4. Make modality switching explicit

Layer selection should have been modeled as a small state machine instead of ad hoc branching.

It should have answered:

- does this layer own a raster overlay?
- does this layer own a nav track?
- should existing overlays remain visible?
- what should the inspector show?
- what should processing/export enable?

That would have prevented the earlier "non-sidescan selection clears the map" inconsistency.
It also would have prevented the later split-brain behavior between selection, panel navigation, status messaging, and workspace changes.

### 5. Break UI shell construction into smaller builders

The toolbar, menu, activity bar, context panel, properties panel, and main viewport are all legitimate subsystems. They should have been split into small builder/helper classes or separate source files once the class crossed "simple shell" size.

A healthier target would have been:

- `MainWindow.Shell.cpp`
- `MainWindow.Menus.cpp`
- `MainWindow.Actions.cpp`
- `MainWindow.Project.cpp`
- `MainWindow.Layout.cpp`

That keeps reviewable units small without changing runtime behavior.

## Stage 01 Requirements for the XTF Parsing Workflow

### 1. Treat cache and raw paths as one logical read pipeline

This is the design rule Stage 01 must enforce:

- the reader and the index must always come from the same artifact store

If the app falls back from cache to raw, it must also rebuild or swap the index source. Reusing cache-derived offsets against the raw file should never have been possible.

The correct design is:

- `ArtifactStoreSession`
  - resolved path
  - resolved format
  - opened reader
  - index built from that same store

Then every downstream read uses that session, not loose path/index combinations.

That same session should also have owned import metadata:

- survey name
- vessel name
- time span
- frequency
- spatial reference
- source fingerprint

### 2. Separate indexing, decoding, and persistence more cleanly

The current pipeline works, but the concerns are still close together:

- source inspection
- reader selection
- index build
- cache generation
- project persistence
- UI signaling

What should have existed conceptually:

- `FormatProbe`
  - identify format/capabilities
- `IndexBuilder`
  - scan raw file and build artifact index
- `ArtifactStoreWriter`
  - write parsed cache
- `ArtifactStoreReader`
  - open raw or cache consistently
- `ImportTransaction`
  - update project/source/layer state atomically

- `LayerActivationLoader`
  - lightweight map activation
  - progressive/windowed decode
  - background swath enrichment

This makes failures easier to isolate and easier to test.

### 3. Design the XTF reader around explicit invariants

The XTF reader does a lot of good defensive work, but the invariants should have been documented and tested from day one.

Important invariants:

- every index entry points to a valid artifact store offset
- `file_offset` and `subrecord_offset` are meaningful only for the store that produced them
- navigation backfill never mutates timestamps
- projected vs geographic coordinates are carried consistently into index, cache, and decoded artifacts
- corrupted records are skipped safely without poisoning later seeks

Those rules should have been written down beside the reader implementation.

### 4. Add fixtures for malformed and messy XTFs early

The parser currently shows strong defensive instincts, but confidence should come from tests, not only from careful code.

The XTF workflow should have had fixture coverage for:

- zero-coordinate pings with `PACKET_NAV` backfill
- mixed channel metadata quality
- `BytesPerSample` missing or wrong
- split-packet channel layouts
- projected nav units
- truncated records
- oversized/corrupt record lengths
- cache rebuild after source change
- raw/cache decode parity

Without those fixtures, every parser improvement carries quiet regression risk.

### 5. Persist metadata as first-class import results

Survey name, vessel name, time span, frequency, spatial reference, and source fingerprint should have been treated as durable import facts, not incidental values.

That means:

- write them at import time
- restore them on project load
- refresh them on reindex
- keep them consistent across layers sharing one source

That should have been part of the base data model contract from the beginning.

Just as importantly, cache reuse should not have been able to silently downgrade those values.

### 6. Make import failure modes visible and recoverable

Import is not just a parse job. It is a user workflow.

The workflow should have clearly distinguished:

- source file missing
- duplicate import
- stale cache
- cache unreadable
- raw parse failed
- project save failed

For each state, the UI should have offered a stable recovery path instead of relying on implicit fallback behavior.

The same principle applies to successful-but-heavy states:

- indexed but not decoded
- decoded enough for extents only
- visible swath loaded
- full swath cache available

## What "Done Right" Would Look Like

### MainWindow

- `MainWindow` stays under control as a shell/orchestrator
- feature behavior lives in controllers/services
- unfinished actions are hidden or clearly disabled
- action state is derived from a single policy model
- layer switching behavior is explicit and modality-safe
- workspace and panel state have one source of truth
- transient messages do not fight persistent context text

### XTF Workflow

- raw and cache reads are unified behind one resolved artifact-store session
- indexing and decoding are separately testable
- metadata persistence is part of the import contract
- cache/raw parity is tested
- malformed XTF fixtures exist and are run automatically
- full-line decode is not required just to activate a layer on the map
- coordinate-space semantics stay correct from reader to UI labels

## Stage 01 Exit Criteria

Stage 01 should not be considered complete until all of these are true:

- panel and workspace transitions go through one real shell-state path
- toolbar, properties panel, and workspace state no longer rely on dead or split state
- persistent context text and transient job messaging no longer fight over one status channel
- feature flags are either honored honestly or unfinished surfaces are hidden/disabled
- artifact-store/index consistency is enforced across cache-hit, raw-read, and fallback paths
- cache-hit and raw-import paths return the same durable metadata contract
- simple layer activation no longer requires full-line decode on the UI thread
- regression coverage exists for:
  - cache/raw alignment
  - metadata persistence
  - nav backfill
  - stale cache invalidation

## Out Of Scope For Stage 01

Stage 01 is not the place to finish everything:

- it should not try to claim broad "all XTF" compatibility
- it should not become a full performance-tuning sweep beyond blocker removal
- it should not become the final import-once UX pass

Its job is to make the later stages safe to implement.

## Recommended Next Steps

### Immediate

- keep `MainWindow` as the integration shell, but stop adding new feature policy there
- add regression tests for cache/raw alignment and nav backfill
- hide or disable remaining Phase-2 actions
- fix the split workspace state paths so menu actions and toolbar actions go through the same shell transition code
- assign and manage the right toolbar through real owned state instead of a dead member

### Short Term

- extract window action policy from `MainWindow`
- introduce an artifact-store session abstraction for raw/cache consistency
- split `MainWindow.cpp` into smaller implementation units
- move status messaging and context display into separate UI channels
- stop full-line sidescan decode during simple layer activation

### Medium Term

- add XTF fixture-based parser tests
- add import workflow tests around reindex, duplicate import, and source changes
- define documented invariants for artifact index, metadata, and spatial reference propagation
- add scale/performance tests for large XTF project open and layer activation

## Concrete Implementation Outputs

At the end of this stage, Claude should be able to point to concrete outputs, not only code movement:

- one documented shell-state path for panels/workspaces
- one documented artifact-store/session contract
- one documented import metadata contract
- one initial regression test set for the highest-risk import/cache paths
- one closure note describing what Stage 02 can now safely assume

## Bottom Line

The right work was not a rewrite. It was earlier boundary-setting.

`MainWindow` should have been kept smaller and more declarative.
The XTF pipeline should have been made more testable and more explicit about store/index consistency.

The codebase already has the bones for that direction. The main gap was not ambition or structure. It was letting working code outrun its contracts.
