# Stage 08 Slice 114 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-114 — asynchronous completion and stale-result integrity
- primary goal: keep concurrent jobs mutually exclusive where required and prevent an older background completion from mutating or reappearing in newer project/view state

## What Changed

- `ProcessingService::runAll` now reserves every output path before dispatch, so a concurrent batch or single-layer run cannot enter the same sidecar writer. Every reserved path is released on normal or exceptional completion.
- Future-result exceptions in single and batch processing are converted into `runFailed`/`batchComplete` outcomes instead of escaping a Qt completion callback or leaving active-path bookkeeping stuck.
- Each `ImportJobManager::ActiveJob` now carries the project epoch captured at dispatch. Starting a newer job can no longer make an older post-cancel completion appear current merely by changing the manager-wide active-job snapshot.
- Waterfall/sub-bottom replacement clears pending debounces and keyed operations; metadata loads use monotonically increasing generations; 3D terrain uses per-layer generations plus a pending-generation set. Superseded or removed-layer results are discarded.
- Import rebuild, import-review probe, waterfall rebuild, metadata load, and terrain completion paths now catch future exceptions and surface a failure/retain the last good view rather than unwinding through the event loop.

## Files Touched

- `src/app/services/ProcessingService.cpp`
- `src/app/import/ImportJobManager.{h,cpp}`
- `src/app/services/ImportService.cpp`
- `src/ui/features/import/ImportReviewWizard.cpp`
- `src/ui/features/waterfall/WaterfallViewData.cpp`
- `src/ui/features/waterfall/WaterfallWindow.{h,Lifecycle.cpp,Repipe.cpp}`
- `src/ui/features/waterfall/WaterfallWindowLoad.cpp`
- `src/ui/features/subbottom/SubBottomWindow.{h,Load.cpp}`
- `src/ui/features/metadata/SSSMetadataLoad.cpp`
- `src/ui/features/metadata/SBPMetadataWindow.Load.cpp`
- `src/ui/features/map/MapView3D.{h,cpp,Load.cpp}`
- `tests/test_processing_service.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- New focused `ProcessingService` coverage passed: a batch reserves its path synchronously, rejects a competing single run, reports the batch failure, releases the reservation, and permits a later run.
- Focused `ImportClassifier` verification passed after the import-manager epoch change; event-path inspection confirms long-lived jobs compare their own stored epoch at terminal callbacks.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `PerfBaseline` and `GlSmoke`.

## Gate Status

- gate items completed: processing jobs cannot concurrently publish to the same path; failed futures release reservations; stale import/view/terrain completions are generation-filtered.
- gate items still open: a dedicated fake-ImportService test could exercise every `ImportJobManager` cancel/re-dispatch interleaving directly.

## Risks / Follow-Ups

- Cancellation is cooperative. A worker already in non-interruptible parsing or file publication may finish its private work, but its obsolete UI/model completion is suppressed.
- Viewer generation guards protect state replacement; they do not attempt to pre-empt third-party decoder calls that are already executing.

## What The Next Stage May Assume

- A background result is accepted only for the layer/project generation that dispatched it.
- `ProcessingService` owns output-path exclusion for both single and batch runs and releases that exclusion on every handled terminal path.
