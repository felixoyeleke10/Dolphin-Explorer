# Stage 07 Slice 04 Closure — "Loading into map" Now Part of the Loading Process

## Symptom
After a background import/decode task finished, the global loading indicator
went idle while the map was still building its swath — the user saw the loading
process report "done" even though "Loading into map…" was still happening. The
map-build phase was tracked by the import dialog (`m_pending_map_loads`) but was
invisible to the app-wide busy check.

## Root Cause
`IViewerWindow::viewerDataState()` defaults to `ViewerDataState::Idle`, and
`SidescanViewController` (the map's sidescan controller, registered as a viewer)
never overrode it. `WindowRegistry::anyViewerBusy()` — the single source of truth
for `MainWindow::refreshLoadingIndicator()` — therefore always saw the map
controller as `Idle`, even while an `activateLayer` background build was in
flight. When any other viewer (waterfall/SBP) finished and called
`refreshLoadingIndicator()`, the indicator was hidden prematurely.

Separately, the controller's own `loadingStarted`/`loadingFinished` handlers in
`MainWindow.cpp` poked the status bar directly (`setProgressIndeterminate()` /
`hideProgress()`), so an SSS build finishing would hide the indicator even if
another viewer was still busy.

## Fix
1. `SidescanViewController` now reports real busy state:
   - New members `m_data_state` (default `Idle`) and `m_active_builds` (count of
     in-flight `activateLayer` tasks — layers can load concurrently).
   - `viewerDataState()` override returns `m_data_state`.
   - `activateLayer()` sets `Loading` and `++m_active_builds` when a background
     task is launched (alongside `emit loadingStarted()`); the watcher's finished
     handler decrements and, when the last build ends, sets `Ready`.
   - `deactivate()` resets the counter and state to `Idle` (in-flight tasks are
     cancelled there).
2. `MainWindow.cpp` routes the SSS `loadingStarted`/`loadingFinished` signals
   through `refreshLoadingIndicator()` instead of toggling the status bar
   directly, so the indicator reflects the aggregate busy state of all viewers
   (including concurrent map builds). `onMapLoadDone()` for the import dialog is
   unchanged.

Net effect: `anyViewerBusy()` now stays true until the map swath actually
finishes building, so the loading indicator no longer clears while "Loading into
map…" is still in progress.

## Files Changed
- `src/ui/features/map/sidescan/SidescanViewController.h` — `viewerDataState()`
  override + `m_data_state` / `m_active_builds` members
- `src/ui/features/map/sidescan/SidescanMapLoadTask.cpp` — set Loading at launch,
  clear at build completion
- `src/ui/features/map/sidescan/SidescanMapDiagnostics.cpp` — reset state in
  `deactivate()`
- `src/ui/mainwindow/MainWindow.cpp` — route SSS loading signals through
  `refreshLoadingIndicator()`

## Build Result
All four changed translation units compiled with **0 errors, 0 warnings**;
`dolphin-ui-map` and `dolphin-ui-mainwindow` static libraries relinked cleanly.
The final `DolphinExplorer.exe` relink was blocked only by `LNK1168` (the app was
running) — completes once the running instance exits.
