# Stage 08 Slice 116 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-116 — background reader and correction lifetime decoupling
- primary goal: remove borrowed QObject/service lifetimes from worker captures and prevent correction completions from crossing a project transition

## What Changed

- Thread-safe artifact-store readers on `ImportService` are now static functions over copied paths/indexes. They no longer require a live `ImportService` QObject merely to decode immutable store inputs.
- Sidescan and sub-bottom correction services no longer retain or capture an `ImportService*`; their workers call the static readers from value-copied requests.
- Waterfall, sub-bottom, SSS/SBP metadata, sidescan-map, and SBP-map loaders dropped their borrowed import-service members/inputs where the worker only needed store-reading helpers.
- Worker-to-UI progress callbacks use `QPointer` owners before queueing UI work, so a destroyed controller/window cannot be dereferenced by a late progress report.
- `CorrectionBatchOperator` records a generation for standalone and batch correction jobs. Project transition increments the generation, drains queued work, marks diagnostics cancelled, and discards terminal signals from already-running old-generation services.
- MainWindow invokes correction invalidation from `projectAboutToChange`, before the replacement project is bound.

## Files Touched

- `src/app/services/ImportService.{h,cpp}`
- `src/app/services/ImportService.Load.cpp`
- `src/app/corrections/SidescanCorrectionService.{h,cpp}`
- `src/app/corrections/SubBottomCorrectionService.{h,cpp}`
- `src/ui/mainwindow/coordinators/CorrectionBatchOperator.{h,cpp}`
- `src/ui/mainwindow/MainWindow.cpp`
- `src/ui/features/map/sidescan/SidescanMapLoadParams.h`
- `src/ui/features/map/sidescan/SidescanMapLoadTask.{cpp,Build.cpp}`
- `src/ui/features/map/sidescan/SidescanViewController.{h,cpp}`
- `src/ui/features/waterfall/WaterfallWindow.{h,Lifecycle.cpp}`
- `src/ui/features/waterfall/WaterfallWindowLoad.cpp`
- `src/ui/features/subbottom/SubBottomWindow.{h,Load.cpp}`
- `src/ui/features/metadata/SSSMetadataWindow.h`
- `src/ui/features/metadata/SSSMetadataLoad.cpp`
- `src/ui/features/metadata/SBPMetadataWindow.{h,Load.cpp}`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.Sbp.cpp`

## Tests Or Validation

- The MSVC/Ninja tree rebuilt successfully after the static-reader and constructor-signature migration, before the later independent UI file splits.
- Repository search confirms the affected worker inputs no longer carry `ImportService*`, and correction terminal handlers consume a recorded job generation before emitting persistence/failure state.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `PerfBaseline` and `GlSmoke`.

## Gate Status

- gate items completed: immutable artifact reads no longer borrow a service QObject lifetime; late progress callbacks are guarded; old-project correction completions cannot persist into the newly bound project.
- gate items still open: a deterministic correction-service seam would allow direct unit coverage of cancellation during each write phase.

## Risks / Follow-Ups

- A correction already inside its atomic store write is allowed to finish that durable file operation. Generation filtering suppresses its old-project terminal mutation; it is not unsafe thread termination.
- Static readers remain read-only helpers. Import queue state and signals continue to belong to the `ImportService` instance on the application thread.

## What The Next Stage May Assume

- Background store decoding depends on copied value inputs, not the lifetime of an `ImportService` QObject.
- Project change invalidates both queued and running correction-result generations before new project state is exposed.
