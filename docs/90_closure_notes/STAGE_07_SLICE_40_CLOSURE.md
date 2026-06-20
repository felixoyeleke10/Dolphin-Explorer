# Stage 07 — Slice 40: Decompose ContactManagerWindow monolith

## Goal
Break the monolithic `ContactManagerWindow.cpp` (~1689 lines) into aspect files that
comply with the project's split convention (`MainWindow.*` / `DataLibraryWindow.*`),
without changing behaviour.

## What changed
Split into one shared visuals unit plus three aspect files behind the unchanged
`ContactManagerWindow.h`:

| File | Lines | Responsibility |
|------|-------|----------------|
| `ContactVisuals.{h,cpp}` | 49 / 178 | Shared model constants (columns, nav kinds, item roles), colour/label helpers, `ChipDelegate`, `makeContactThumb`. Namespace `dolphin::ui::cmvis`. |
| `ContactManagerWindow.cpp` | 234 | Core: ctor, `setProject`, `refresh`. |
| `ContactManagerWindow.Layout.cpp` | 260 | `buildNavBar`, `buildCommandBar`, `buildPreviewPane`. |
| `ContactManagerWindow.View.cpp` | 511 | Nav history, `rebuildNav`, `populateForCurrentNode`, search, breadcrumb, command/status state, `setViewMode`, `updatePreview`. |
| `ContactManagerWindow.Commands.cpp` | 371 | Selection helpers, clipboard (cut/copy/paste), rename/favourite, recycle-bin actions, export, custom-group ops, nav context menu. |

Each aspect `.cpp` pulls the shared unit in with
`#include "ui/features/contacts/ContactVisuals.h"` + `using namespace dolphin::ui::cmvis;`.

`src/ui/CMakeLists.txt` (`dolphin-ui-contacts`) now lists the four new sources.

## Compliance
- No behavioural change — pure mechanical decomposition; the previously-monolithic
  unit is gone. Largest file is now 511 lines (View), within the file-size policy.
- Layer rules unchanged: feature stays in `ui`, depends on `dolphin-app` / `dolphin-util`
  / `dolphin-ui-shared`.
- Mutations still route through MainWindow's undo stack via signals (no direct
  project-state edits in the window) — no band-aid introduced.

## Verification
- `cmake --build .` clean (full link of `DolphinExplorer.exe`).
- App launches and stays up.
