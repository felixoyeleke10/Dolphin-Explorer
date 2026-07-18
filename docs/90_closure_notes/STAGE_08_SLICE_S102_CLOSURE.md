# Stage 08 Slice S-102 Closure — Uniform SSS Processing

## What changed

- Added `SidescanProcessingCoordinator` as the single SSS processing workflow.
- Centralized target filtering, duplicate suppression, scope handling, change
  classification, and display/navigation commits in that coordinator.
- Kept `DisplayStateManager` as the sole SSS model mutation authority.
- Made external waterfall updates signal-silent.
- Removed waterfall draft-time processing triggers; Apply is now the execution boundary.
- Prevented main-window Apply from re-entering or widening waterfall Apply signals.
- Removed active-line gain/contrast propagation into the map's global display state.
- Unified the waterfall palette read with the canonical global SSS key.
- Made waterfall command-palette Apply use the same commit signals as visible Apply controls.

## Files touched

- `src/ui/features/waterfall/WaterfallWindow.cpp`
- `src/ui/features/waterfall/WaterfallWindow.Params.cpp`
- `src/ui/features/waterfall/WaterfallWindow.Toolbar.cpp`
- `src/ui/features/waterfall/WaterfallWindow.h`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanelImage.cpp`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanelProcessing.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`
- `src/ui/mainwindow/coordinators/SidescanProcessingCoordinator.h`
- `src/ui/mainwindow/coordinators/SidescanProcessingCoordinator.cpp`
- `src/ui/systems/DisplayStateManager.cpp`
- `tests/test_sidescan_processing_coordinator.cpp`

## Validation

- Static signal-path audit for every waterfall Apply entry point.
- Static state-write audit for SSS display state and global map display params.
- Incremental MSVC/Ninja waterfall and main-window UI-library builds.
- Focused CTest run: `SidescanProcessingCoordinator`, `ProjectStorage`,
  `RasterCache`, and `NavCorrections` all passed.

## Remaining risk

- Widget-level visual synchronization tests remain desirable, but coordinator
  scope and per-modality isolation now have headless regression coverage.

## Stage gate

- This closes the confirmed cross-window SSS state-drift paths. Other Stage 08 criteria remain unchanged.
