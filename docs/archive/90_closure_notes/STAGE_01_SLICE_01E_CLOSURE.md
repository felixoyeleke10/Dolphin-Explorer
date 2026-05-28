# Stage 01 Slice 01E Closure

## Scope

- active stage: `Stage 01`
- active slice: `01E activation/loading correction plus tests`
- primary goal: stop simple layer activation from requiring full-line sidescan decode on the UI path

## What Changed

- changed `SidescanViewController` activation to load a bounded preview window with `loadSidescanWindow(...)` instead of `loadAllSidescanPings(...)`
- kept exact projected source CRS stamping on the preview path so cache/raw activation stays spatially consistent
- moved track-length status to artifact-index-derived metadata so the controller no longer depends on full decoded pings just to report line length
- updated `MapView::addSidescanPings(...)` to preserve an existing full nav track/bbox when only a swath preview is loaded
- stopped `MainWindow::loadProject(...)` from auto-decoding every indexed sidescan layer during project open

## Files Touched

- `src/ui/SidescanViewController.cpp`
- `src/ui/MapView.cpp`
- `src/ui/MainWindow.cpp`

## Tests Or Validation

- direct compile check passed for `src/ui/SidescanViewController.cpp`
- direct compile check passed for `src/ui/MapView.cpp`
- direct compile check passed for `src/ui/MainWindow.cpp`
- full executable link was not rerun because `build_mingw/DolphinExplorer.exe` is still running and would block the final link step

## Gate Status

- gate items completed:
  - layer activation no longer requires a full sidescan decode
  - project open no longer auto-decodes all indexed sidescan layers
  - preview swath loading now stays bounded while preserving full track extents
- gate items still open:
  - automated regression coverage for Stage 01 remains missing
  - Stage 01 still needs a final stage-level closure note after test-harness work is addressed

## Risks / Follow-Ups

- the initial swath shown after selection is now a bounded preview window, not a full-line decode
- `MapView.cpp` should get a formatting cleanup on a later pass; the logic compiles cleanly, but the wrapped preview-preservation block is not as tidy as it should be
- the controller status labels still rely on preview pings for sample position/depth, so a future pass can make those labels CRS-aware and less preview-dependent

## What The Next Stage May Assume

- selection/open flows no longer force full-line sidescan decoding just to put a layer on the map
- full-track extents can come from the artifact index while swath detail is loaded separately in bounded slices
- later performance/scalability work can build on bounded preview loading instead of undoing eager activation behavior
