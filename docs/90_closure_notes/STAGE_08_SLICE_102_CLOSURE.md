# Stage 08 — Slice 102 Closure: Views ▸ SSS Blend mode + Dynamic-range histogram

## Goal
Continue closing the gap to the SeaView layer-properties reference (slices
100–101). Add the two most reachable remaining controls: **Blend mode** and a
**Dynamic-range histogram**. Both implemented for real (D-05 — no stub UI).

## Blend mode (per-layer, 2D mosaic compositing)
Controls how a line's sonar image composites over lines already drawn beneath
it — visible where surveys overlap.
- `DataLayer::map_blend_mode` (int; 0=Blend, 1=Cover up, 2=Lighten, 3=Darken),
  persisted as an optional v11 field (default 0 omitted).
- `LayerMapData::blend_mode`, seeded from the model in `setLayerMapData`.
- `MapViewPaint.Sonar.cpp` applies a per-layer `QPainter` composition mode +
  opacity: Blend = SourceOver @0.88, Cover up = SourceOver opaque, Lighten =
  CompositionMode_Lighten, Darken = CompositionMode_Darken.
- `DisplayStateManager::setLayerBlendMode` (emits Opacity aspect — same cheap
  map-composite fan-out as transparency); `MapView/MapViewportHost::
  setLayerBlendMode` (2D only — the 3D drape/curtain draw one texture per
  layer with no overlap blending). Live: a repaint, no re-raster.
- ViewsPanel: a combo on the SSS page.

## Dynamic-range histogram (black/white points)
A custom histogram widget with two draggable handles that set
`SonarDisplayParams::display_low`/`display_high` — the normalised amplitude
window mapped through `SSSAmplitudeProcessor::displayIntensity` before gain/
contrast.
- New widget `ui/shared/widgets/HistogramRangeSlider.{h,cpp}`: sqrt-compressed
  bars (sonar amplitude piles up near zero), two handles, in-range tint, live
  `rangeChanged` + on-release `rangeCommitted`.
- Data source: the **SSS controller**, not the map. New
  `SidescanViewController::amplitudeHistogram(layer_id)` + `autoStretch(...)`
  read `m_layer_intensity_cache` (uint16 amplitude pixels). This was a real
  correction found in verification — `LayerMapData::intensity_cache` on the map
  is empty; the pixels live controller-side (that is what `repaletteAllLayers`
  recolors from).
- Handles seat on the layer's effective auto-stretch bounds when display_low/
  high are still at identity (0/1), so they show where the real black/white
  points sit rather than pinned to the edges.
- Apply: on drag release, write display_low/high to the layer's params via
  `setLayerSssDisplay` and re-raster once via `applyLiveCorrections` (the
  raster cache key includes the params, so it rebuilds with the new stretch).
  Committed-on-release, not debounced-per-tick — one rebuild per gesture.

## Persistence / tests
- Schema stays v11 (`map_blend_mode` is another optional v11 field alongside
  `map_opacity`; both uncommitted this session).
- `test_project_storage.cpp` `testMapOpacityPersistence` extended to cover
  `map_blend_mode` round-trip (customised=Lighten restored; default=Blend
  stays 0). Suite 153 → 155 checks; full ctest 16/16 green.

## Verification (temp diag, removed after)
SSS project, first layer selected:
- histogram = 96 bins, 49,594 sampled counts, peak 1243 (real distribution);
  auto-stretch 0.0086 / 0.976 seats the handles.
- `setLayerBlendMode(2)` → model 2 AND `LayerMapData::blend_mode` 2 (fan-out
  reaches the renderer).

## SeaView gap now
Implemented across slices 100–102: Palette, Gain, Contrast, Transparency,
Blend mode, Dynamic range. Still unimplemented (no renderer path, recorded):
Coverage-only rings, Clipping polygons, Show discarded areas, Show side-scan
beams, Interval (pings). These need genuinely new render features (coverage
ring draw mode, polygon clip mask, beam-ray geometry) and are the honest
remaining backlog — no faking them per D-05.
