# Stage 08 Slice 140 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-140 — display-only project open (operator-driven processing)
- primary goal: opening a project displays persisted work and never starts
  processing; mosaics build only when the operator acts

## Why

After the georeferencing rework changed the raster fingerprints, opening the
real survey triggered ~10 minutes of unrequested background processing: every
stale line was decoded + re-rasterized at open, then every line was staged up
to the requested quality tier — disk and CPU churn exactly when the operator
was trying to work ("the app is incredibly slow… doing too much processing
instead of letting the user do it themself"). This also cut against locked
policy D-06 (index-first, visible-first; no eager hydration).

## What Changed

- `SidescanViewController::activateLayer` gained `cache_only` (and now returns
  whether the line was deferred). cache_only displays persisted work only:
  the best already-fresh raster tier at or below the current quality loads
  (cheap disk read, no decode); if none exists the line stays as its nav
  track. Never decodes pings, never rasterizes, never stages background tier
  upgrades from this path.
- Project open (`firstLayerReady`): every non-active SSS line activates
  cache_only; every non-active SBP line shows its nav track only (the profile
  ribbon is a disk-heavy trace read — it builds on selection). The
  "sbp:open" ribbon fan-out and its lane are gone.
- The reindex-completion path (`cacheLayerReady`) follows the same policy:
  recovered non-active lines show track + any persisted raster; no automatic
  rasterization fan-out after a recovery.
- The ACTIVE (restored) line keeps its normal cache-first load — reopening a
  project still restores exactly what the operator was looking at, and with
  healthy caches that is a cache hit, not a build.
- After open, a status message reports how many lines were deferred:
  "N line(s) shown as nav track from saved data — select a line to build its
  map imagery."
- Operator actions are unchanged and remain the ONLY build triggers:
  selecting a line, Apply/Apply-to-All, changing Sonar preview quality.

## Files Touched

- `src/ui/features/map/sidescan/SidescanViewController.h`
- `src/ui/features/map/sidescan/SidescanMapLoadTask.cpp`
- `src/ui/mainwindow/MainWindow.cpp`
- `src/ui/mainwindow/MainWindow.Controllers.cpp`

## Tests Or Validation

- Empirical on the real survey (temp diag, removed after): deleted every
  raster tier for a non-active line, opened the project — 30 s later ZERO
  rasters had been built (nav track shown, deferral reported). Selecting the
  line then built exactly that line's raster once (persisted q3, no
  speculative tier upgrades).
- Cache reuse separately confirmed earlier: lines with fresh rasters load
  from disk without rebuild across sessions.
- Full rebuild clean; ctest 24/24 (PerfBaseline excluded, unaffected).

## Risks / Follow-Ups

- First open after a georef/params change now shows nav tracks instead of
  silently rebuilding for minutes — the status message tells the operator
  why and what to do. An explicit "Build survey mosaic" batch action (all
  lines, progress cards) would be a natural companion; the per-line path via
  selection/Apply covers it today.
