# Stage 07 · Slice 89 — SBP shows on the main map at project open (SSS parity)

## Goal (user direction)
"SBP seems to be behind unlike SSS" in the main window. Diagnosis: two map
population paths treated only Sidescan as map-worthy —
1. The project-open loop loaded track + raster for every indexed SSS line,
   but skipped SubBottom entirely: SBP lines stayed INVISIBLE on the map
   until each was manually selected once (buildSbpProfileMap only ran from
   onLayerSelected).
2. cacheLayerReady (reindex completion) had the same SSS-only branch.

## What changed (MainWindow.cpp)
- **Project open**: the per-layer loop now also handles SubBottom — instant
  nav track from the artifact index (showNavTrackFromIndex is modality-
  agnostic: zero I/O, ~1000 decimated points) and a background
  `buildSbpProfileMap` per line on a new **"sbp:open" lane, cap 2** (trace
  reads are disk-heavy — D-14 spirit; SSS rasters use the map lane, cap 2).
  The depth-coloured ribbon replaces the plain track when built.
- **cacheLayerReady**: reindexed non-active SBP lines get the same instant
  track + ribbon build (previously: nothing until selected).
- onLayerSelected's existing guarded build stays (covers the auto-selected
  first line and anything missed).

## Verification
Build green; 16/16 tests. Live check on the SBP-Only recent project: opened
via session controller with NO layer selection — the depth ribbon appears,
fitted, on the main map. Both available recents are single-line projects, so
the multi-line case (where the fix visibly matters most) follows the exact
same call as the proven selection path; confirm on a real multi-line SBP
survey.
