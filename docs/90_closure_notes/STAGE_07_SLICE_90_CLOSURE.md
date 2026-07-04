# Stage 07 · Slice 90 — SBP map lines: plain nav style + full interaction parity

## Goal (user direction)
Follow-up to slice 89: (1) "zoom, move SBP data in map … not working
effectively like SSS"; (2) "the SBP nav line appeared colored, horrible —
it's a navigation line."

## Root causes
1. **Rainbow ribbon**: Profile-kind layers were overpainted with a
   depth-coloured HSV ribbon (blue→green→red) — visually wrong for a nav
   line, and expensive: one QPen construction + state change PER SEGMENT per
   frame (tens of thousands on real lines) on every pan/zoom repaint.
2. **No interaction**: hitTestLayer and layersInRect tested ONLY swath
   coverage polygons — SBP/MAG/track layers had no click-select, no
   rubber-band select, no hover highlight, no tooltip on the map.
3. **Paint cost**: the combined nav track (repainted every frame) received
   SBP tracks at full trace resolution (one point per trace).

## What changed
- **paintProfileTracks deleted** (MapViewPaint.NavTrack/.cpp, MapView.h) —
  SBP lines render through the combined white nav-line pass like every other
  modality. Depth belongs to the SBP viewer / 3D curtains, not the map line.
- **Hit-testing** (MapViewInput.cpp): coverage-less layers with a nav track
  are picked by point-to-segment distance (6 px tolerance, bbox pre-reject);
  rubber-band (layersInRect) tests track points/segments against the rect.
  Click-select, Ctrl-multi-select, hover highlight, and tooltips now work on
  SBP lines exactly as on SSS swaths.
- **Selection/hover visuals** (MapViewPaint.Sonar.cpp): for track-only layers
  the accent outline / dashed hover is the nav polyline itself
  (drawLayerOutline helper: swath hull when coverage exists, else the line).
- **Display decimation** (MapView::rebuildCombined): each layer contributes
  at most ~3000 points to the combined painted track (NaN segment breaks and
  endpoints preserved). Full-resolution data stays in LayerMapData for
  hit-testing, 3D, and stats.

## Verification
Build green; 16/16 tests. Live check (SBP-Only project, auto-open, no
clicks): the SBP line renders as the standard white nav line — no rainbow.
Interaction paths compile-verified against the same geometry helpers the
paint uses; confirm feel on a real multi-line survey.
