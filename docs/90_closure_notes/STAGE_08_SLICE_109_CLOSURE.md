# Stage 08 Slice 109 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-109 — MainWindow tool/export ownership extraction
- primary goal: reduce MainWindow feature ownership, remove stale commands, and keep every exposed export action backed by working code

## What Changed

- Added `ToolController` as the single transition path for map tools. It synchronizes the 2D map, 3D viewport, shared `AppState`, and exclusive toolbar buttons instead of duplicating that state logic across MainWindow slots.
- Added `ExportController` to own export dialogs, CSV/report/screenshot/raster file I/O, the lazy Export Manager window, and export result reporting. MainWindow now retains only composition-level delegates.
- Reduced `MainWindow.Tools.cpp` from 615 lines to 255 lines by moving state ownership and consolidating the duplicated sidescan navigation/heading correction dialog path.
- Removed enabled or registered KMZ, navigation CSV, and generic PDF export commands and their dead shared-command metadata because they had no implementation. The implemented GeoTIFF path is now the export command exposed by the toolbar and command palette.
- Removed the dead Merge Lines signal/action path and obsolete MainWindow export-window/action bookkeeping.

## Files Touched

- `src/ui/mainwindow/coordinators/ToolController.{h,cpp}`
- `src/ui/mainwindow/coordinators/ExportController.{h,cpp}`
- `src/ui/mainwindow/MainWindow.Export.cpp`
- `src/ui/mainwindow/MainWindow.Tools.cpp`
- `src/ui/mainwindow/MainWindow.{h,cpp}`
- `src/ui/mainwindow/MainWindow.Menus.cpp`
- `src/ui/mainwindow/MainWindow.ToolBar.cpp`
- `src/ui/mainwindow/MainWindow.Commands.cpp`
- `src/ui/CMakeLists.txt`
- `src/ui/shared/AppCommands.{h,cpp}`
- `src/ui/shared/panels/LineListPanel.{h,ContextMenu.cpp}`

## Tests Or Validation

- `dolphin-ui-mainwindow` rebuilt successfully with MSVC/Ninja, including generated MOC output for both new controllers.
- Repository search confirms the removed export slots, MainWindow export window/action members, merge request signal, and merge handler have no remaining references.
- Export menu, toolbar, and command palette now converge on the same implemented CSV, GeoTIFF, screenshot, report, and manager workflows.

## Gate Status

- gate items completed: tool state has one owner; export file I/O is outside MainWindow; visible export commands are implemented; duplicate correction-dialog construction is removed.
- gate items still open: dialog-driven export workflows do not yet have an injectable file-dialog seam for unit testing.

## Risks / Follow-Ups

- `ExportController` still combines several closely related export formats. Split format writers behind a service interface if non-interactive/batch export is added.
- MainWindow remains the application composition root by design; future feature policy should continue moving into coordinators rather than growing its aspect files.

## What The Next Stage May Assume

- Map tool synchronization is owned by `ToolController`.
- Export workflows are owned by `ExportController`, while MainWindow only connects shell commands to that controller.
- No shipped command promises the removed unimplemented KMZ, navigation CSV, generic PDF, or merge workflow.
