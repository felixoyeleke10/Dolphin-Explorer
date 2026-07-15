# Stage 08 Slice 118 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-118 — remaining UI translation-unit decomposition
- primary goal: split the largest remaining mixed-responsibility UI implementation files without moving product policy into MainWindow

## What Changed

- Split `ContactEditorDialog` into dialog/image-shell construction, form construction, and contact/form state synchronization. The resulting files are 226, 318, and 388 physical lines.
- Split `MapViewInput` into core pointer/tool event handling, feature editing, and hit-testing/selection geometry. The resulting files are 457, 57, and 192 physical lines.
- Split `MainWindow.LayerCoordinator` into layer-selection/lifecycle coordination, SBP map construction, and raster display. The resulting files are 428, 136, and 163 physical lines.
- Registered every new translation unit in the owning UI CMake target; no new cross-layer dependency or product-policy owner was introduced.

## Files Touched

- `src/ui/features/contacts/ContactEditorDialog.cpp`
- `src/ui/features/contacts/ContactEditorDialog.Form.cpp`
- `src/ui/features/contacts/ContactEditorDialog.State.cpp`
- `src/ui/features/map/MapViewInput.cpp`
- `src/ui/features/map/MapViewInput.Features.cpp`
- `src/ui/features/map/MapViewInput.HitTesting.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.Sbp.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.Raster.cpp`
- `src/ui/CMakeLists.txt`

## Tests Or Validation

- Definition/reference review confirms each moved method remains in the original class/namespace and appears in exactly one translation unit.
- CMake source registration covers all six new aspect files.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `PerfBaseline` and `GlSmoke`.

## Gate Status

- gate items completed: the contact editor, map input controller, and layer coordinator no longer combine all responsibilities in one 600-plus-line implementation file; each extracted aspect has a bounded concern.
- gate items still open: MainWindow remains the composition root and its coordinator aspects should continue to stay declarative as features evolve.

## Risks / Follow-Ups

- These are class-preserving translation-unit splits, not new ownership boundaries. If any extracted concern gains independent state/policy, promote it to a feature coordinator/service rather than sharing more MainWindow internals.

## What The Next Stage May Assume

- Contact form/state logic, map hit-testing/feature editing, and layer SBP/raster construction can be reviewed and rebuilt independently at the translation-unit level.
- No remaining file in these three clusters carries the full former mixed responsibility set.
