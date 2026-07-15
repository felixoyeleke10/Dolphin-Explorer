# Stage 08 Slice 127 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-127 — dual-mode TVG editor
- primary goal: let Sidescan users edit TVG as an interactive graph or with
  the existing numeric coefficients

## What Changed

- Added an interactive TVG gain-versus-range graph with draggable handles at
  50 m and 200 m, axes in metres and dB, and a shared PORT/STBD curve.
- Added a persisted `Graph` / `Numeric` editor selector to the TVG section.
- Graph handle changes solve back to the existing spreading and absorption
  coefficients; numeric changes update the graph. Both modes therefore feed
  the same processing algorithm and project parameters.
- Graph is the default editor mode; the user's editor preference is retained
  in `QSettings`.

## Files Touched

- `src/ui/features/waterfall/components/TvgCurveEditor.{h,cpp}`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanel.{h,cpp}`
- `src/ui/features/waterfall/panels/WaterfallAnalysisPanelImage.cpp`
- `src/ui/CMakeLists.txt`
- `docs/90_closure_notes/STAGE_08_SLICE_127_CLOSURE.md`

## Tests Or Validation

- `dolphin-ui-waterfall` compiled successfully.
- Full executable link validation.
- `git diff --check`.

## Gate Status

- gate items completed: TVG has graph and numeric editing over one model
- gate items still open: none for this bounded Sidescan editor slice

## Risks / Follow-Ups

- The graph represents the current physical two-coefficient TVG model rather
  than introducing arbitrary per-point corrections, preserving project and
  processing compatibility.

## What The Next Stage May Assume

- TVG graph and numeric modes are interchangeable views of the same correction.
