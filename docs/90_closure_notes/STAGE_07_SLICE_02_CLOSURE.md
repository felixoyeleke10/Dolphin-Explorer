# Stage 07 Slice 02 Closure — Bottom-Track → DLPD → Map Seabed Correction Fix

## Goal
Fix the broken chain where bottom-track picks detected in the SSS waterfall viewer were
never persisted to the DLPD, and the map georeferencer always used `nav.altitude_m` instead
of the more accurate detected pick range.

## Root Cause Analysis

Two independent bugs prevented seabed correction from reaching the map:

1. **Georeferencer ignored `bottom_pick`** — `SidescanSwathGeoreferencer.cpp` and
   `SssCoverageBuild.cpp` always used `ping.nav.altitude_m` for slant→ground conversion,
   regardless of whether a `bottom_pick` was set on the ping.

2. **Viewer picks never reached DLPD** — `SidescanCorrectionService::applyToLine()` loaded
   pings from the DLPD, applied TVG/ARC/AGC, and wrote them back — but the updated
   `bottom_pick` fields from the viewer's in-memory pings were never passed in.
   `ParsedCache` already had the infrastructure to read/write `bottom_pick` fields;
   they just weren't populated.

## Fix Architecture

### Fix 1 — Georeferencer prefers bottom_pick (2 files)

Both `georeferenceSidescanPings` and `buildSwathCoverage` now use:
```cpp
const double altitude_m = (ping.bottom_pick.valid() && ping.bottom_pick.source > 0)
    ? static_cast<double>(ping.bottom_pick.range_m)
    : std::max(0.0, static_cast<double>(ping.nav.altitude_m));
```

`source > 0` means the pick is either auto-detected (1) or user-edited (2).
Untracked pings (`source == 0`) fall back to nav altitude as before.

### Fix 2 — Viewer picks merged in the same DLPD write pass

**Critical issue resolved**: `WaterfallView::rawPings()` returns `m_raw_pings` (pings as
loaded from disk). Seabed detection results are NOT written back into `m_raw_pings.bottom_pick`;
instead they are stored in `m_rows[i].seabed` (auto-detected, current window) and
`m_manual_seabed` (user-edited, survives window changes). Simply calling `rawPings()` would
see no picks.

**Fix**: Added `WaterfallView::applySeabedPicksToPings(pings)` in `WaterfallViewSeabed.cpp`
that builds a `timestamp_us → (range_m, source)` map from both structures:
- `m_rows` for auto-detected picks (source=1), excluding is_manual rows
- `m_manual_seabed` for user edits (source=2), overriding auto-detected

Applied to a copy of `rawPings()` — `WaterfallWindow::currentRawPings()` now returns pings
with `bottom_pick` correctly populated from viewer seabed state.

Extended `SidescanCorrectionService::applyToLine()` with an optional `viewer_pings` parameter
(default `{}`). In `execute()`, after amplitude corrections, a timestamp-keyed lookup merges
viewer picks into the loaded DLPD pings — a single write pass handles both amplitude AND picks.

`CorrectionBatchOperator::applySSS()` threads `viewer_pings` through to the service.

### Trigger points (MainWindow.WaterfallCoordinator.cpp)

Two trigger points ensure picks reach the DLPD:

1. **`bakeCurrentLine`** (connected to `GainControlPanel::applyToLineRequested` and
   `ImagingControlPanel::applyToLineRequested`): now passes
   `m_waterfall_win->currentRawPings()` alongside the correction params.
   Existing workflow: user tunes gain + runs bottom tracking → "Apply to Line" → both saved.

2. **SRC change handler** (in the `paramsApplied` lambda): when the SRC flag flips,
   if the viewer holds detected picks (`source>0 && range_m>0`), kicks off an async
   `applySSS` with the viewer pings. The immediate `reloadLayer` call satisfies the UI
   for the no-picks case; the async bake fires a second SSS reload (via
   `correctionsPersisted → postLayerDataChanged`) with the correct pick-based altitude.

## Files Changed

- `src/ui/features/map/sidescan/SidescanSwathGeoreferencer.cpp` — prefer bottom_pick
- `src/ui/features/map/sidescan/SssCoverageBuild.cpp` — prefer bottom_pick
- `src/app/corrections/SidescanCorrectionService.h` — `viewer_pings` param + SidescanPing include
- `src/app/corrections/SidescanCorrectionService.cpp` — picks merge in execute(); pass viewer_pings
- `src/ui/mainwindow/coordinators/CorrectionBatchOperator.h` — `viewer_pings` param
- `src/ui/mainwindow/coordinators/CorrectionBatchOperator.cpp` — thread viewer_pings to service
- `src/ui/features/waterfall/WaterfallWindow.h` — `currentRawPings()` declaration + SidescanPing include
- `src/ui/features/waterfall/WaterfallWindow.Repipe.cpp` — `currentRawPings()` implementation
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp` — pass viewer_pings in bakeCurrentLine; picks bake on SRC change

## Build Result
All 16 targets clean, 0 errors, 0 warnings.
