# Stage 06 — Slice 01 Closure: ProjectSessionController Extraction

## What shipped

Extracted all project-lifecycle state and CRUD operations out of `MainWindow` into a new `ProjectSessionController` (PSC) QObject:

**New files:**
- `src/ui/mainwindow/ProjectSessionController.h` — declares PSC with signals, slots, and minimal accessors
- `src/ui/mainwindow/ProjectSessionController.cpp` — implements new/open/save/saveAs/close/autoSave, addToRecentProjects, buildWindowTitle

**Moved out of MainWindow:**
- Members: `m_project`, `m_project_dirty`, `m_project_load_gen`, `m_save_in_progress`, `m_pending_crs`
- Methods: `loadProject()`, `addToRecentProjects()`, `setWindowTitleFromProject()`
- All CRUD slots are now thin delegates on MainWindow that call into PSC

**MainWindow wires four PSC signals:**
- `projectAboutToChange` → suppress viewport updates, deactivate SSS, cancel pending ops
- `projectChanged(proj)` → `bindProjectUi()`, re-enable viewport on null project
- `firstLayerReady(id)` → re-enable viewport, activate layers (deferred tick)
- `jobMessage(msg)` → intercepts `__import_cache__:<path>` prefix for cache-file import routing; else `appendJobMessage()`

**Adapter helpers on MainWindow** (so 16 aspect files need no verbose `m_session_ctrl->project().get()` calls):
- `currentProject()` → raw `app::Project*`
- `currentProjectPtr()` → `std::shared_ptr<app::Project>`
- `isProjectDirty()` → bool
- `markProjectDirty()` → marks dirty + emits windowTitleChanged via PSC

**Files touched:** 16 mainwindow aspect files + 5 coordinator files + CMakeLists.txt

## Decisions made

- `adoptNewProject()` does NOT emit `projectAboutToChange`/`projectChanged` — the import flow creates projects inline without viewport suppression ceremony.
- Cache-file routing uses a `__import_cache__:<path>` sentinel in `jobMessage` so PSC doesn't need a back-reference to MainWindow.
- `firstLayerReady` is deferred with `QTimer::singleShot(0)` + load-gen guard to avoid Win32 ShowWindow mid-call-stack reentrance on Windows.

## What's next

- Slice 02: `LayerDisplayCoordinator` — extract `m_active_layer_id`, `onLayerSelected()`, `updateNavigationButtons()`, and layer-visibility state into a coordinator so MainWindow doesn't own display policy.
