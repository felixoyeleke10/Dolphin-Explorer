# Stage 08 Slice 121 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-121 — final structural and UI-consistency cleanup
- primary goal: finish bounded translation-unit decomposition and replace accidental or non-functional UI behavior with explicit compatibility and shipped-surface policy

## What Changed

- Split project reconstruction into a 174-line manifest/source orchestrator, a 544-line layer hydration aspect, and a 249-line contact/feature/group aspect. Shared spatial-reference and stored-path read codecs live in `Project_p.h`, and `fromJson` restores sources, layers, then entities exactly once.
- Split the former monolithic dialog stylesheet into a 13-line aggregator plus chrome, import, contact, and progress aspects, each no larger than 274 physical lines. Tokenization and composition order remain centralized.
- Reduced the Line List context-menu implementation to a 44-line item-type dispatcher. Project, layer/source, annotation/group, and shared menu-building concerns now live in separate aspect files with a small private helper contract; the largest aspect is 278 lines.
- Moved 3D layer picking, selection dispatch, and ground-ray intersection from the general `MapView3D.cpp` implementation into `MapView3D.Input.cpp`, alongside the mouse/tool behavior that consumes them.
- Removed the duplicate Assistant title-bar dropdown whose “New Conversation,” “Configure,” and “Manage” entries performed no work. The implemented Conversation button remains available; the shell no longer advertises the inert duplicate surface.
- Replaced the accidental `@font`/`@fontXxx` prefix-collision behavior with an explicit inherited-size compatibility policy. Dormant explicit-size declarations are removed cleanly before font-family substitution, preserving the established visual scale without malformed QSS or an unrequested app-wide reskin.

## Files Touched

- `src/app/project/Project.Serialization.Read.cpp`
- `src/app/project/Project.Serialization.Layers.Read.cpp`
- `src/app/project/Project.Serialization.Entities.cpp`
- `src/app/project/Project.h`
- `src/app/project/Project_p.h`
- `src/app/CMakeLists.txt`
- `src/ui/shell/AppStyle.cpp`
- `src/ui/shell/AppStyleDialogs*.cpp`
- `src/ui/shell/AppStylePrivate.h`
- `src/ui/shared/panels/LineListPanel.{h,ContextMenu.cpp}`
- `src/ui/shared/panels/LineListPanel.ContextMenu.*.cpp`
- `src/ui/shared/panels/LineListPanel.ContextMenu_p.h`
- `src/ui/features/map/MapView3D.cpp`
- `src/ui/features/map/MapView3D.Input.cpp`
- `src/ui/mainwindow/MainWindow.Chrome.cpp`
- `src/ui/CMakeLists.txt`
- `tests/test_app_style.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- Definition/reference review confirms each extracted project, stylesheet, context-menu, and 3D-input method/helper has one implementation and every new translation unit is registered with its owning CMake target.
- Added the `AppStyle` CTest target. It builds both dark and light stylesheets and rejects unresolved `@font` tokens or malformed `sans-serifXxx` remnants from prefix substitution.
- Targeted diff checks passed for both project-persistence extractions. The final serial MSVC/Ninja build passed, and CTest passed 23/23.

## Gate Status

- gate items completed: the remaining mixed-responsibility persistence and UI files are split into reviewable aspects; the title bar no longer exposes a no-op Assistant menu; typography compatibility is intentional and produces valid QSS without changing the tuned visual scale.
- gate items still open: aggregate build/test verification and a manual dark/light visual smoke pass remain part of the final QC gate.

## Risks / Follow-Ups

- These are class-preserving translation-unit splits. If layer hydration, menu actions, or style sections acquire independent state or policy, promote that concern to a dedicated service/controller instead of widening the aspect again.
- Explicit `@fontXxx` sizes remain disabled by compatibility policy. Enabling them later is a deliberate typography redesign and should carry screenshot/visual review rather than being treated as a token-order bug fix.
- The layer-read aspect is still the largest project reconstruction file because cache recovery and legacy mixed-layer migration are one load contract. Split those paths further only with focused persistence regression coverage.

## What The Next Stage May Assume

- Project manifest orchestration, layer hydration, and entity reconstruction can be reviewed independently without duplicated codec or restore calls.
- Dialog styles and Line List context actions have bounded contribution points, while 3D picking is co-located with input behavior.
- The current inherited typography scale is an explicit compatibility choice, and no inert Assistant workflow remains on the shipped title-bar surface.
