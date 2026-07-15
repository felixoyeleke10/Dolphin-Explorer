# Stage 08 Slice 128 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-128 — shared TVG header mode switch
- primary goal: place the Numeric/Graph switch in the TVG header on both
  Sidescan control surfaces

## What Changed

- Added a shared compact two-button `TvgEditorModeSwitch` with graph and
  numeric choices and a persisted common preference.
- Waterfall Image Processing now places the switch in the TVG header and
  removes the body-level Editor dropdown.
- Main-window Gain Controls now uses the same header switch.
- Both surfaces show exactly one representation at a time: coefficient rows
  in Numeric mode or the gain curve in Graph mode.

## Files Touched

- `src/ui/features/waterfall/components/TvgEditorModeSwitch.{h,cpp}`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanel.{h,Image.cpp}`
- `src/ui/mainwindow/panels/GainControlPanel.{h,cpp}`
- `src/ui/CMakeLists.txt`
- `docs/90_closure_notes/STAGE_08_SLICE_128_CLOSURE.md`

## Tests Or Validation

- Waterfall and MainWindow UI libraries compiled successfully.
- `DolphinExplorer.exe` linked successfully.
- `git diff --check` passed.

## Gate Status

- gate items completed: both TVG surfaces share one header mode-switch pattern
- gate items still open: none for this bounded UI-systemization slice

## Risks / Follow-Ups

- Numeric and Graph remain representations of the same two-coefficient model.

## What The Next Stage May Assume

- TVG editor-mode placement and persistence are consistent between the main
  window and Waterfall window.
