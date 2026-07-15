# Stage 08 Slice 108 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-108 — persistence authority and save-failure honesty
- primary goal: ensure project mutations never report durable success through a save path that discards failure

## What Changed

- Processing/correction completion no longer calls `Project::save()` directly. `ProjectOperationCoordinator` marks the project modified, then requests persistence from `ProjectSessionController`, the owner of dirty/clean state and diagnostics.
- Successful immediate saves now clear the dirty marker/title through the session controller; failed auto-saves leave the project dirty, add an Error to Diagnostics, and emit an actionable job message.
- Removed the misleading `ProjectTransaction` helper. It mutated live state immediately, performed a fallible save from its destructor, ignored the result, and did not roll back despite its name/documentation.
- CRS and waterfall mutations now mark dirty explicitly and request the same session-owned auto-save path.
- Removed the redundant unconditional save of an already-clean project during application close; there was no changed state to persist and its failure was ignored.

## Files Touched

- `src/ui/mainwindow/coordinators/ProjectOperationCoordinator.{h,cpp}`
- `src/ui/mainwindow/ProjectSessionController.cpp`
- `src/ui/mainwindow/MainWindow.cpp`
- `src/ui/mainwindow/MainWindow.Events.cpp`
- `src/ui/mainwindow/MainWindow.Geodesy.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`
- `src/app/project/ProjectTransaction.h` (removed)

## Tests Or Validation

- `dolphin-ui-mainwindow` rebuilt successfully with MSVC/Ninja, including regenerated MOC output for the new coordinator signal.
- Repository search confirms no `ProjectTransaction` references remain.
- Repository search confirms direct MainWindow/coordinator `Project::save()` calls are gone; lifecycle persistence remains centralized in `ProjectSessionController`.

## Gate Status

- gate items completed: failed background/immediate manifest saves are visible and retain dirty state; successful saves synchronize session state; misleading destructor-side persistence is removed.
- gate items still open: save-failure injection at the UI-controller level would require a filesystem seam or mockable project store and remains a testing follow-up.

## Risks / Follow-Ups

- Temporary projects intentionally remain dirty because `autoSave()` does not promote them or silently choose a permanent path; normal Save/close prompting still owns that decision.
- Repeated disk failures can add repeated diagnostics on repeated auto-save attempts; deduplication can be added in DiagnosticsHub if it becomes noisy.

## What The Next Stage May Assume

- MainWindow feature code marks mutations dirty and routes persistence through `ProjectSessionController`.
- No project save is hidden in an RAII destructor, and an auto-save failure is operator-visible.
