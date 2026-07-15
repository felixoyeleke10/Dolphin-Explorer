# Stage 08 Slice 125 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-125 — acoustic-viewer explorer top alignment
- primary goal: keep dataset names at the top of the explorer section in both
  Sidescan Waterfall and Sub-bottom viewers

## What Changed

- The Sidescan `Files` list is explicitly top-aligned within its collapsible
  section body.
- The Sub-bottom `Lines` list uses the identical alignment rule.
- This prevents Qt from vertically centering a height-capped list when its
  section body receives surplus height.

## Files Touched

- `src/ui/features/waterfall/panels/WaterfallInspectorPanel.Layout.cpp`
- `src/ui/features/subbottom/panels/SubBottomInspectorPanel.Layout.cpp`
- `docs/90_closure_notes/STAGE_08_SLICE_125_CLOSURE.md`

## Tests Or Validation

- Compile and full-link validation.
- `git diff --check`.

## Gate Status

- gate items completed: both acoustic explorers use top-aligned dataset lists
- gate items still open: none for this bounded layout slice

## Risks / Follow-Ups

- The lists retain their existing 120 px maximum height and scrolling behavior.

## What The Next Stage May Assume

- Sidescan and Sub-bottom explorer dataset lists share the same vertical
  alignment policy.
