# Stage 07 — Slice 74: Main-window SSS processing controls → full parity with waterfall

## Request
Unify the SSS processing tools between the main window and the waterfall (chosen
approach: shared panels / full parity). QC (slice 73) found the main window exposed a
simplified subset while the waterfall had the full control set.

## What was done
Brought the main-window right-panel sections to **full functional parity** with the
waterfall over the shared `WaterfallParams` contract (both sides produce/consume it):

- **Radiometry (`GainControlPanel`) — AGC expanded** from strength-only to the full set:
  Mode (Global/Variable), Strength, Along-Track Window (Variable-only, shown
  conditionally), Smoothing Type (Mean/Median), Smoothing Window, Edge Skip, Noise Floor.
  (TVG and ARC were already full.)
- **Enhancement (`ImagingControlPanel`)** — Destripe gained Window + Subdivision (had
  Capping); Beam Pattern gained Smooth Radius; ML Enhance gained Tile Pings + Tile Samps
  (had Clip Limit). ARN already full.
- Ranges/defaults/visibility mirror the waterfall exactly; `writeInto`/`setParams`/
  enable-state updated; signal-blocked restores.
- **Navigation + Geometry:** already ≥ the waterfall (smooth + layback + heading/pitch/
  roll), so no change needed.

The main window and waterfall now expose the **same knobs with the same ranges**, all
mapping to the same `WaterfallParams` fields.

## Intentional differences (kept)
- **Slant Range Correction** stays waterfall-only (prior user decision).
- **Seabed Picking** stays waterfall-only (it's a waterfall editing tool).
- Section grouping/names differ (Radiometry/Enhancement/Nav/Geometry vs Image
  Processing/Processing/Nav) — organizational, not a control gap.

## Implementation note / deferred
This delivers parity by expanding the two main-window panels (safe, contained) rather
than replacing the mature waterfall panel with one shared widget class — that literal
single-implementation consolidation would require rewiring the waterfall's live-apply
guards + visibility + dirty tracking, which can't be validated without a GUI run. The
`WaterfallParams` contract already makes the two behave identically; consolidating to one
widget class is a safe follow-up once GUI-testable.

## Verification
- Build green; `ctest -E PerfBaseline` → 16/16.
- NEEDS VISUAL CHECK: main-window Radiometry AGC shows Mode/Strength/(Along-Track in
  Variable)/Smoothing Type/Window/Edge Skip/Noise Floor; Enhancement shows the full
  Destripe/BPN/ML controls; values apply and round-trip.

## Files
- `src/ui/mainwindow/panels/GainControlPanel.{h,cpp}`, `ImagingControlPanel.{h,cpp}`
