# Stage 08 Slice 112 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-112 — MainWindow composition-root decomposition
- primary goal: eliminate the 900-line constructor translation unit without changing startup order or moving product policy back into MainWindow

## What Changed

- Extracted process-wide state, diagnostics, operation-manager, import-service, and processing-service initialization into `MainWindow.Runtime.cpp`.
- Extracted sidescan, correction, import-execution, processing, and persistence coordinator construction/wiring into `MainWindow.Controllers.cpp`.
- Kept the constructor as an ordered composition recipe: runtime services, project-session wiring, shell construction, event wiring, feature controllers, title bar, and persisted UI restoration.
- Removed translation-unit dependencies that the reduced constructor no longer uses.
- Added Visual Studio and local `out/` directories to repository ignores so IDE/build analysis does not pollute change review.
- Reduced `MainWindow.cpp` from 907 lines to 388 lines; the two new responsibility-focused files are 273 and 284 lines.

## Files Touched

- `src/ui/mainwindow/MainWindow.cpp`
- `src/ui/mainwindow/MainWindow.Runtime.cpp`
- `src/ui/mainwindow/MainWindow.Controllers.cpp`
- `src/ui/mainwindow/MainWindow.h`
- `src/ui/CMakeLists.txt`
- `.gitignore`

## Tests Or Validation

- `dolphin-ui-mainwindow` rebuilt successfully with MSVC/Ninja after the extraction and generated MOC refresh.
- Startup call order is unchanged; the extracted methods are invoked at the exact former inline positions.
- The complete MSVC/Ninja application build passed, followed by all 19 CTest targets with zero failures (104.10 seconds total on the final tree).

## Gate Status

- gate items completed: the active MainWindow file is no longer a monolithic service/controller wiring unit, and each extracted file has one bounded composition responsibility.
- gate items still open: MainWindow intentionally remains the application composition root and still owns project-session/event-bus connection setup.

## Risks / Follow-Ups

- The wiring methods contain UI-thread signal lambdas by design. Future feature behavior should move into feature coordinators/services, leaving these methods as connection declarations only.

## What The Next Stage May Assume

- Runtime-service setup and feature-controller setup can evolve independently without regrowing the MainWindow constructor.
