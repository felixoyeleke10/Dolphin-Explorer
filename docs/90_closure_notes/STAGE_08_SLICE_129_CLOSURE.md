# Stage 08 Slice 129 Closure — Waterfall Contact Picking Redesign

## Behavioral goal

Replace the flat Waterfall contact controls with distinct automatic-detection and manual-picking workflows.

## Delivered

- Split Contact Picking into clearly labelled **Automatic Detection** and **Manual Picking** sections.
- Kept the panel control-focused: Automatic contains Classification and Sensitivity without explanatory copy; Manual contains no classification selector.
- Added conservative, balanced, and sensitive automatic scan modes.
- Implemented automatic candidate detection against the currently loaded physical-amplitude rows using a bright-target and following-shadow heuristic.
- Routed detected candidates through the existing contact persistence signal path so they behave like project contacts rather than temporary UI marks.
- Prevented repeat scans from adding near-duplicate contacts at the same row, channel, and range.
- Preserved classification selection, manual point picking, review/edit, and clear-all workflows.
- Added purpose-specific primary-action and helper-text styling consistent with the acoustic-view panels.
- Reports the scan result count in the Waterfall status bar.

## Verification

- `cmake --build . --target dolphin-ui-waterfall --parallel 1` (MSVC/Ninja): passed.
- Final incremental verification after duplicate suppression: passed.

## Files

- `src/ui/features/waterfall/WaterfallView.h`
- `src/ui/features/waterfall/WaterfallViewData.cpp`
- `src/ui/features/waterfall/WaterfallWindow.cpp`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanel.h`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanelContact.cpp`
- `src/ui/shell/AppStyleAcousticViews.cpp`
