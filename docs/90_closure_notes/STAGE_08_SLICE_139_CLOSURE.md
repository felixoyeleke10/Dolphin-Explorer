# Stage 08 Slice 139 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-139 — nav-repair operator transparency
- primary goal: the ONLY automatic transformation in the georeferencing
  pipeline (bounded GPS interpolation) must be visible to the operator, per
  the professional-tool principle that nothing is applied to data silently

## Context

Audit against the SonarWiz/SeaView operator-control model found the app
already gates every data-affecting operator behind explicit settings + Apply
(gain/imaging chain off-by-default, nav smoothing Off, layback off, SRC off,
nav/heading source combos, display-vs-Bake separation per D-04). The one
automatic step — bounded interpolation between trusted GPS fixes, which is a
mathematical necessity for slow-cadence GPS — was counted
(`NavStats::interpolated_nav`, `kNavFlagInterpolated`) but never surfaced.

## What Changed

- `MainWindow.Diagnostics.cpp` (`onMapDiagnosticsReady`): a per-line Info
  entry in the Problems panel whenever repaired pings exist:
  "N of M pings (X%) positioned by bounded interpolation between GPS fixes —
  expected for slow GPS cadence; positions are never invented across line
  breaks or ping resets."
- Deliberately Info severity (never Warning): at 0.1–1 Hz GPS under a 5–20 Hz
  ping rate, interpolation between fixes is how georeferencing works in every
  commercial tool; the goal is visibility, not alarm.

## Files Touched

- `src/ui/mainwindow/MainWindow.Diagnostics.cpp`

## Tests Or Validation

- Live end-to-end on the real SSS survey (temp diag, removed after): the
  active line reported "900 of 1802 pings (49.9%) positioned by bounded
  interpolation…" as a per-layer Info problem.
- Full rebuild clean; ctest 24/24 (PerfBaseline excluded, unaffected).

## Risks / Follow-Ups

- Optional next step for even stronger transparency: tint interpolated track
  segments on the map (the per-ping flag is already in the corrected nav
  table) and show measured/repaired counts in the Import Review Wizard when
  it lands.
