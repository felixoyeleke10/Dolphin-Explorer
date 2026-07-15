# Stage 08 Slice 115 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-115 — project lifecycle, storage ownership, and manifest validation
- primary goal: make project creation/deletion/switching fail safely, preserve durable parsed assets, and reject malformed identity/reference state at the manifest boundary

## What Changed

- Removing a layer or opening a project no longer deletes unreferenced `.dlpd` / `.dpcache` stores. Only rebuildable `.draster` sidecars remain eligible for orphan cleanup, preserving parsed artifacts as durable workflow assets under D-04.
- A completed import whose layer was removed while indexing settles leaves its DLPD durable and reports the missing layer; it no longer unlinks the store opportunistically.
- Project deletion sends only the manifest and the canonical project data directory to the Recycle Bin. It never selects source survey paths or arbitrary layer artifact paths outside that owned root.
- `Project::create` rejects an empty/existing manifest path and an unavailable/non-directory data path. The new-project flow constructs and validates the replacement before emitting the project-transition signal.
- Manifest loading catches parser exceptions, requires non-empty unique source/layer/contact/feature identities, validates layer-to-source references, bounds persisted enum/range/index values, and returns a descriptive load error instead of allowing unchecked numeric casts or broken references into the model.
- New/open/close/delete/application-close now refuse to transition while imports still hold project state. The UI truthfully asks the operator to wait rather than promising immediate abandonment that teardown cannot provide.
- Node-graph edits now mark the project dirty and route persistence through the session-owned auto-save path.

## Files Touched

- `src/app/project/Project.cpp`
- `src/app/project/Project.Layers.cpp`
- `src/app/project/Project.Serialization.Read.cpp`
- `src/app/services/ImportService.Private.h`
- `src/app/services/ImportService.Tasks.cpp`
- `src/app/services/ImportService.cpp`
- `src/ui/mainwindow/ProjectSessionController.{h,cpp}`
- `src/ui/mainwindow/MainWindow.Events.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.NodeGraphCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp`
- `tests/test_project_creation.cpp`
- `tests/test_project_storage.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- Focused `ProjectCreation` verification passed for data-directory creation, existing-manifest preservation, an uncreatable parent, and a data path occupied by a file.
- Focused `ProjectStorage` verification passed for external parsed-store and unreferenced project-store preservation. The subsequently added malformed-number, identity/reference, and persisted-range cases are registered for the combined rerun.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `PerfBaseline` and `GlSmoke`.

## Gate Status

- gate items completed: project creation is non-destructive; deletion is constrained to project-owned paths; D-04 assets survive layer removal/open; malformed manifest identities/references fail closed; active imports block unsafe lifecycle transitions.
- gate items still open: true interruption of an already-running import remains a separate cancellation-lifetime design rather than being simulated by a misleading confirmation dialog.

## Risks / Follow-Ups

- The canonical data directory is project-owned. Projects should continue using one manifest/project root rather than sharing that directory with unrelated user files.
- Range sanitization protects model invariants; it does not attempt to infer the operator's intended value from a malformed manifest.

## What The Next Stage May Assume

- `.dlpd` and legacy `.dpcache` files are durable assets, not layer-removal/open-time cache garbage.
- A successful `Project::create` has a usable canonical data directory and did not overwrite an existing manifest.
- Project replacement/deletion cannot proceed while imports still retain the current project.
