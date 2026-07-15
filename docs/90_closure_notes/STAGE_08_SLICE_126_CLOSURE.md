# Stage 08 Slice 126 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-126 — centralize amplitude-chart visibility
- primary goal: make Waterfall Settings the sole UI surface for the Sidescan
  amplitude profile chart toggle

## What Changed

- Removed the `Amp. Chart` toggle from the Waterfall inspector's View Settings.
- Removed its inspector signal, state-sync method, widget member, and window
  wiring.
- Retained the existing `Show amplitude profile bar` checkbox in Waterfall
  Settings, including persistence and live Apply behavior.
- Updated the settings tooltip so it no longer refers to the removed inspector
  control.

## Files Touched

- `src/ui/features/waterfall/panels/WaterfallInspectorPanel.{h,Layout.cpp,Data.cpp}`
- `src/ui/features/waterfall/WaterfallWindow.cpp`
- `src/ui/features/waterfall/WaterfallWindow.Params.cpp`
- `src/ui/features/waterfall/WaterfallSettingsDialog.cpp`
- `docs/90_closure_notes/STAGE_08_SLICE_126_CLOSURE.md`

## Tests Or Validation

- Compile and full-link validation.
- Search validation for removed inspector amplitude-toggle symbols.
- `git diff --check`.

## Gate Status

- gate items completed: one authoritative UI location for amplitude-chart visibility
- gate items still open: none for this bounded Sidescan settings slice

## Risks / Follow-Ups

- Existing persisted `waterfall/showAmpBar` values remain compatible.

## What The Next Stage May Assume

- Amplitude-chart visibility is configured only through Waterfall Settings.
