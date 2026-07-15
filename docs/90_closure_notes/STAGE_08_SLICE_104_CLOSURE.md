# Stage 08 Slice 104 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-104 — unreachable implementation removal
- primary goal: remove dead and duplicate code that could not affect the shipped application, including null-only integration plumbing

## What Changed

- Removed the stale `ui/views/nodegraph/NodeGraphWindow.cpp` copy. It was outside CMake, included headers that no longer exist, and duplicated the live `ui/features/nodegraph` implementation.
- Removed the unbuilt `CompoundNode` / `TemplateDefinition` prototype. No production or test code referenced it, and its implementation was not linked into `dolphin-pipeline`.
- Removed the legacy floating `LayerPickerWidget`. The shell replaced it with `LineListPanel`; no code ever constructed the picker, yet its always-null pointer was still passed through import, visibility, rename, resize, and project-binding paths.
- Simplified `ExecutionController` so it drives only the active progress dialog and status signals.

## Files Touched

- `src/pipeline/CompoundNode.{h,cpp}` (removed)
- `src/pipeline/TemplateDefinition.h` (removed)
- `src/ui/views/nodegraph/NodeGraphWindow.cpp` (removed)
- `src/ui/shared/widgets/LayerPickerWidget.{h,cpp}` (removed)
- `src/ui/CMakeLists.txt`
- `src/ui/features/import/ImportController.{h,cpp}`
- `src/ui/mainwindow/MainWindow.{h,cpp}` and affected MainWindow aspect/coordinator files

## Tests Or Validation

- Repository-wide reference search is clean for `LayerPickerWidget`, `m_layer_picker`, `ui/views/nodegraph`, `CompoundNode`, and `TemplateDefinition`.
- CMake regenerated the Ninja graph successfully.
- Full MSVC incremental build completed successfully, including `DolphinExplorer.exe` and all rebuilt test targets.

## Gate Status

- gate items completed: eliminated 1,066 lines of unreachable or duplicate implementation without changing shipped behavior; reduced MainWindow/import null plumbing.
- gate items still open: further MainWindow ownership extraction and the remaining repository-wide correctness/UI findings are separate slices.

## Risks / Follow-Ups

- No runtime behavior depended on the removed code. Any future compound-node or floating-picker work must be reintroduced as an intentionally built, tested feature rather than revived from stale prototypes.

## What The Next Stage May Assume

- The live node-graph UI exists only under `src/ui/features/nodegraph`.
- Import completion updates the active `LineListPanel`/project event paths; there is no legacy floating layer picker.
