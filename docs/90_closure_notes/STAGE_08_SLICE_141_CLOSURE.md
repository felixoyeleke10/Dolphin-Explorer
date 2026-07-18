# Stage 08 Slice 141 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-141 — operator toggle for the near-nadir band
- primary goal: the blank strip along the vessel track becomes an explicit
  operator choice (Views ▸ SSS "Show nadir band"), defaulting to showing data

## Context

After the georeferencing rework, uncorrected mosaics rendered the near-nadir
zone as an open gap: `sssInnerGapMetres` excluded real seabed samples whose
flat-bottom ground range fell below the altitude, as a QC-honesty choice that
makes the geometrically compressed near-track zone obvious. The user read the
blank band as missing data ("the main window isn't showing the nadir data").
Samples were never dishonestly placed — every displayed sample already gets a
true flat-bottom ground range on the fly; the gap only HID valid near-nadir
seabed.

## What Changed

- `SssGeorefParams::show_nadir` (default true). `sssInnerGapMetres` returns 0
  when set — and because raster and coverage share that one policy function,
  both stay consistent automatically (the slice-137 shared-nadir contract).
- The controller OWNS the preference: `setShowNadir()` persists it to
  QSettings ("sss/showNadir"); `setGeorefParams()` preserves the current value
  so correction-dialog applies and `{}` resets cannot silently flip it; the
  constructor restores it.
- Raster fingerprint includes `show_nadir`, so toggling invalidates only what
  it must; rebuilds go through the normal visible-mosaic-stays-up swap.
- Views ▸ SSS gains a "Show nadir band" checkbox (persisted, survey-wide).
  Toggling calls `setShowNadir` + `reloadCurrentLayer()` (explicit operator
  action) and announces the rebuild in the status bar.

## Files Touched

- `src/ui/features/map/sidescan/SssGeorefParams.h`
- `src/ui/features/map/sidescan/SssGeometryPolicy.h`
- `src/ui/features/map/sidescan/SidescanRasterCache.cpp`
- `src/ui/features/map/sidescan/SidescanViewController.{h,cpp}`
- `src/ui/features/map/sidescan/SidescanMapDiagnostics.cpp`
- `src/ui/mainwindow/panels/ViewsPanel.{h,cpp}`
- `src/ui/mainwindow/MainWindow.ContextPanels.cpp`
- `tests/test_sidescan_georef.cpp`

## Tests Or Validation

- `testCoverageAndRasterShareNadirPolicy` now opts into the open gap
  explicitly (it tests that path) and gains a new default-preference case:
  with `show_nadir = true` (the default) the strip's inner edge reaches the
  track (< 1 unit) even without slant correction.
- Full rebuild clean; ctest 24/24 (PerfBaseline excluded, unaffected).

## Risks / Follow-Ups

- Because the default changed, existing fresh rasters are stale once more;
  with the display-only open policy (S-140) lines show as nav tracks until
  the operator builds them — the open message explains this. The visual
  confirmation on the real survey (nadir band now filled) is the user's.
