# Stage 07 Slice 11 — QC fixes for the CRS/cursor work (Slices 08–09)

Addresses three findings from a QC review of the working-CRS + cursor-readout
slices.

## 1. (Medium) Mixed-CRS projects could mislead the status badge
`workingCrs()` returns the *dominant* projected source CRS, so a project with
lines in different projected CRSes showed one CRS in the badge while a selected
layer's "Source CRS" differed — with no signal that the project isn't a single
grid.
- Added `Project::hasMixedProjectedSources()` — true when sources span >1 distinct
  projected CRS id.
- `updateContextInfo()` now appends **"(mixed)"** to the badge in that case (the
  dominant CRS is still shown). Single-survey projects are unchanged.

## 2. (Low/Med) No test coverage for the new forward-projection helper
`geo::latLonToProjected()` became user-facing (status-bar readout depends on it).
Added `testLatLonToProjected()` in `tests/test_sidescan_georef.cpp`:
- EPSG:25829 (UTM 29N) and EPSG:32630 (UTM 30N) match `latLonToUtm` where the
  longitude auto-zones to the same zone;
- argument ordering (northing returned before easting; northing in the 4–5 M band);
- **forced-zone correctness** — same point projected into zone 29 vs 30 yields
  clearly different eastings (proves the target's zone is used, not lon-derived);
- geographic / empty / unsupported targets return false (caller falls back to
  lat/lon).

## 3. (Low) `latLonToUtm()` could yield zone 61 at exactly lon 180.0
`zone = (lon+180)/6 + 1` is 61 at the antimeridian. Clamped to `min(60, …)`;
covered by a test asserting `zone ∈ [1,60]` for lon 180. (`latLonToProjected`
was already safe — its zone comes from `parseUtmZone`, validated 1–60.)

## Build / tests
geo + app + ui-mainwindow libs compile clean. `ctest`: **SidescanGeoref**
(rebuilt with the new cases), **NavCorrections**, **ProjectStorage** all pass.
Exe relink still blocked by LNK1168 (app running) — runtime changes (the "(mixed)"
badge) appear once the app is closed and relinked.

## Not changed
The mixed-CRS *cursor reprojection* still uses the dominant `workingCrs()` — for a
genuinely multi-grid project that's a deliberate simplification (one consistent
readout grid); a per-layer readout grid would be a larger design change and was
not in scope.
