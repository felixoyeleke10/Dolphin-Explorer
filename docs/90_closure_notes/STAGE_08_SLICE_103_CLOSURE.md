# Stage 08 — Slice 103 Closure: SSS draping surface (moved) + clipping polygons + side-scan beams

## Goal
Continue matching the SeaView SSS layer-properties panel (slices 100–102).
User requested three more controls on Views ▸ SSS, all built for real (D-05):
draping surface (XYZ), clipping polygons, show side-scan beams. Confirmed via
AskUserQuestion: remove nothing else; MOVE draping surface off the MAP tab to
SSS (one shared project surface); clipping shows sonar INSIDE drawn polygons.

## Draping surface — moved MAP → SSS
The existing project-global draping control (XYZ/CSV terrain for the 3D view)
moved from `buildMapPage` to `buildSssPage`. Same wiring
(drapingBrowse/ClearRequested → onChooseDrapingSurface/applyDrapingSurface,
Project::drapingSurface). It stays enabled regardless of the active line
(project-global, not per-line). MAP page is now Palette + Sonar preview only.
Note: SBP-only projects no longer expose draping (SSS tab hidden) — acceptable
since draping is bathymetry terrain, an SSS/basemap concept.

## Clipping polygons (per-layer, "show inside")
- `DataLayer::map_clip_polygons` (bool), persisted (optional v11 field).
- `LayerMapData::clip_polygons`, seeded in `setLayerMapData`.
- `MapViewPaint.Sonar.cpp`: when a layer has clip on, its mosaic image is drawn
  under `QPainter::setClipPath(union of drawn polygon features)`. Polygon→pixel
  conversion mirrors `paintFeatures` (incl. projected-CRS normalisation). No
  polygons yet ⇒ no clip (never blanks the whole mosaic from an empty toggle).
- Per-layer, so one line can clip while others render fully.

## Side-scan beams (per-layer overlay)
- `DataLayer::map_show_beams` (bool), persisted (optional v11).
- `LayerMapData::show_beams`, seeded.
- `MapViewPaint.Sonar.cpp`: a beam pass over the mosaic. Each coverage ribbon
  is a closed polygon `[inner_0..inner_{m-1}, outer_{m-1}..outer_0]`; beam i is
  the line inner[i]→outer[i] (`ribbon[i]`→`ribbon[n-1-i]`), decimated to ~60
  beams/ribbon so it reads as a fan, thin translucent cyan.

## Plumbing (both toggles)
- `DisplayStateManager::setLayerClipPolygons` / `setLayerShowBeams` (emit the
  Opacity aspect — the shared cheap map-composite fan-out).
- MainWindow Opacity handler pushes opacity+blend+clip+beams to the viewport.
- `MapView` / `MapViewportHost::setLayerClipPolygons` / `setLayerShowBeams`
  (2D mosaic only — no 3D equivalent). ViewsPanel: two checkboxes on SSS.

## Tests / verification
- `test_project_storage.cpp` extended: clip_polygons + show_beams round-trip
  (customised true restored; defaults false). Suite 155 → 159 checks; full
  ctest 16/16.
- In-app (temp diag, removed): SSS page shows Palette / Blend / Transparency /
  Draping surface / Clipping polygons / Show side scan beams / Dynamic range;
  MAP page no longer shows draping. Beams render on the active line's mosaic
  (fan hatching) and not on other lines. Clip enabled on the active line (whose
  sonar doesn't intersect the drawn polygon) blanks that line's mosaic while
  the other lines stay full — per-layer clip confirmed.

## SeaView gap now
Done (slices 100–103): Palette, Gain/Contrast (right-panel Tools), Blend mode,
Transparency, Draping surface, Clipping polygons, Show side scan beams, Dynamic
range. Remaining unimplemented (need genuinely new render features): Coverage-
only rings (100/200/300%), Show discarded areas, Interval (pings). No stubs.
