# Stage 08 Slice S-101 Closure — Remove Duplicate Navigation Execution UI

## What changed

- Removed the waterfall analysis panel's duplicate Navigation Processing section from the UI.
- Removed its dedicated “Run on This Line” and “Run on All Lines” wiring.
- Retained navigation processing in the shared Navigation/Geometry controls and shared Apply workflow.

## Files touched

- `src/ui/features/waterfall/panels/WaterfallAnalysisPanel.cpp`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanel.h`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanelProcessing.cpp`
- `src/ui/features/waterfall/WaterfallWindow.cpp`
- `src/ui/features/waterfall/WaterfallWindow.h`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`
- `src/ui/mainwindow/MainWindow.h`

## Validation

- Incremental MSVC/Ninja UI-library compilation.
- Static verification that the duplicate section is no longer constructed.

## Remaining risk

- None specific to this slice; the shared Apply workflow remains the supported path.

## Stage gate

- This closes the duplicate navigation execution UI only; other Stage 08 criteria are unchanged.
