# Stage 07 Slice 09 — Cursor readout in the project working grid (SonarWiz convention)

## Goal
Continue the CRS work (Slice 08): make the **coordinate readout** report in the
project's survey/working grid (eastings/northings), like SonarWiz, instead of
WGS84 lat/lon — and make the map, waterfall, and sub-bottom readouts all agree.

## Problem
The map renders in WGS84, so the map cursor handler passed `m_map_view->
isProjected()` (false) → the status bar showed lat/lon even when the project
grid is 25829. The waterfall/SBP readouts passed their own `is_projected`
straight through, so the three viewers could disagree.

## Fix
1. **geo: `latLonToProjected(lat, lon, target, n, e)`** (new public) — forward-
   projects WGS84 lat/lon into a target projected CRS using the **target's own
   UTM zone** (parsed via `parseUtmZone`), not a zone auto-derived from longitude
   (which would give wrong eastings near a zone boundary). Refactored
   `latLonToUtm` to share a zone-explicit `utmForward` core — `latLonToUtm`'s
   signature/behaviour is unchanged (NavCorrections + SidescanGeoref tests pass).
   Returns false for geographic/unsupported targets so callers fall back to
   lat/lon.
2. **`MainWindow::showCursorPosition(lat, lon, is_projected)`** (new) — single
   readout path. Already-projected source coords pass through; geographic input
   is reprojected into `Project::workingCrs()` when that grid is a transformable
   projected CRS. Map (`cursorMoved`), waterfall (`onWaterfallCursorUpdated`),
   and SBP (`cursorUpdated`) all route through it → one consistent survey-grid
   readout. Format reuses the existing `formatPosition` ("N … m   E … m").

## Files
- `geo/GeoUtils.h` / `geo/GeoUtils.cpp` — `latLonToProjected` + `utmForward`
  refactor.
- `ui/mainwindow/MainWindow.h` — `showCursorPosition` decl.
- `ui/mainwindow/MainWindow.Shell.cpp` — `showCursorPosition` def + map
  `cursorMoved` routes through it (+ geo/SpatialRef includes).
- `ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp`,
  `MainWindow.SubBottomCoordinator.cpp` — route through `showCursorPosition`.

## Build / tests
Full build green; `DolphinExplorer.exe` relinked. `ctest`: ProjectStorage,
SidescanGeoref, NavCorrections all pass (the latter two guard the geo refactor).

## Runtime-verify (user)
Move the cursor over the map / waterfall / sub-bottom for a 25829 project — all
three should read "N … m   E … m" in the survey grid and agree; for a
geographic-only project they should still read lat/lon.

## Scope note (datum)
`utmForward` uses the WGS84 ellipsoid; ETRS89/NAD83/GDA targets share it to
sub-metre level (fine for a readout). ED50 is approximated (already noted in
`parseUtmZone`). Non-UTM projected CRSes aren't transformable (consistent with
`isTransformableCrs`) and fall back to lat/lon.

## Still deferred
The map/waterfall **rendering** still normalizes to WGS84 (the waterfall path
hard-codes `makeWgs84SpatialRef()`); export in the survey grid and an optional
dual grid+geographic readout remain future work. This slice covers the readout,
which is the visible SonarWiz behaviour.
