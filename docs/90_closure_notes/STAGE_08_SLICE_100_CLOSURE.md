# Stage 08 — Slice 100 Closure: Views ▸ SSS Gain/Contrast/Transparency

## Goal
User referenced a SeaView layer-properties screenshot (Blend mode, Coverage
rings, Palette, Transparency, Draping surface, Clipping polygons, Show
discarded areas, Show side scan beams, Interval, Dynamic range) and asked to
"add the following to sss view." Per D-05 (no stub UI) and the standing
SSS/SBP parity rule, only the subset the renderer can actually drive was
implemented; the rest is recorded as backlog with the reason it's blocked.

**Added:** Gain, Contrast, Transparency on Views ▸ SSS (Palette already
existed). Transparency also added to Views ▸ SBP for parity (Gain/Contrast/
Palette already existed there).

> **Correction (slice 101):** as first shipped, the SBP Transparency control
> was DEAD — `MapView3D::setLayerOpacity` only iterated drapes, and SBP's map
> presence is a *curtain* (no opacity uniform in its shader) with no 2D preview
> image. It moved/persisted `map_opacity` but changed nothing on screen (a
> D-05 violation). Fixed in slice 101 by giving the curtain shader a `uAlpha`
> uniform and fanning opacity out to `m_curtain_layers`. The "applies to the
> 3D curtain" phrasing below was aspirational at the time of writing and is now
> actually true.

**Not added (renderer gap, not a UI oversight):**
- Blend mode, Coverage-only rings (100/200/300%), Clipping polygons, Show
  discarded areas, Show side scan beams, Interval (pings), Dynamic-range
  histogram — none of these have a rendering path in MapView/MapView3D today
  (no coverage-ring draw mode, no polygon clip mask, no beam-ray geometry, no
  histogram widget). Adding the buttons without the renderer behind them would
  violate D-05.

## Model / persistence
- `DataLayer::map_opacity` (float, default 1.0) — new field, `app/layers/DataLayer.h`.
- `Project::kSchemaVersion` bumped 10→11; `map_opacity` written only when
  customised (< 0.999) so untouched manifests stay minimal; absent on read
  defaults to 1.0 (pre-v11 manifests open unaffected).
- `test_project_storage.cpp` `testMapOpacityPersistence`: customised value
  round-trips; untouched layer reads back opaque. Suite: 145 → 153 checks.

## Display-state authority
- `DisplayAspect::Opacity` + `DisplayStateManager::setLayerOpacity(id, [0,1])`
  — single mutate point, same pattern as `setLayerVisible`.
- MainWindow's `displayStateChanged` bus handler fans Opacity out to
  `MapViewportHost::setLayerOpacity` (mirrors the Visibility branch added in
  slice 96).

## Renderers
- `LayerMapData::opacity` (2D) — seeded from `DataLayer::map_opacity` in
  `MapView::setLayerMapData` (single source of truth re-seeded on every
  rebuild, so a mosaic rebuild never resets a user's transparency). 2D paint
  (`MapViewPaint.Sonar.cpp`) multiplies the existing 0.88 base opacity by it.
- `MapView3D::SonarDrape3D::opacity` (3D) — seeded once at drape creation from
  `LayerMapData::opacity` (passed through `MapViewportHost::onLayerDataLoaded`
  → `setSonarDrape`); live updates go through `MapView3D::setLayerOpacity`,
  which multiplies the drape shader's existing 0.85 base alpha.
- `MapView::setLayerOpacity` / `MapView3D::setLayerOpacity` /
  `MapViewportHost::setLayerOpacity` — cheap, no re-raster, just `update()`.

## SSS Gain/Contrast — live, debounced re-raster
Unlike SBP (shader-uniform recolor, instant), SSS gain/contrast are imaging-
chain fields (`SonarDisplayParams`) that require a mosaic re-raster
(`SidescanViewController::applyLiveCorrections` → `prebuildTier`). A 400ms
single-shot debounce (`MainWindow::m_sss_views_display_timer`) collects rapid
spinbox edits into one rebuild instead of one per tick. The existing
`applyLiveCorrections` machinery only rebuilds layers already loaded on the
map, so this is a lightweight variant of the same path the right panel's
"Apply to Line" uses — no progress-dialog cards, since it's a live per-line
tweak, not a batch operation.

## ViewsPanel changes
`panels/ViewsPanel.{h,cpp}`:
- `setSssLayer(has_layer, palette, gain=1.0, contrast=1.0, opacity_pct=100)`
- `setSbpLayer(..., opacity_pct=100)` (new trailing param, defaulted)
- New signals: `sssDisplayEdited(gain, contrast)`, `sssOpacityEdited(pct)`,
  `sbpOpacityEdited(pct)`.
- New shared helper `makeOpacitySpin()` (QSpinBox, 0–100%, " %" suffix) — used
  on both SSS and SBP pages rather than a QSlider (no QSlider styling exists
  in AppStyleShell; the existing QDoubleSpinBox `viewsSpin` pattern was reused
  for visual consistency).

## Verification (temp diag, removed after)
Live scripted check on the SSS project: opened, activated first layer,
`setLayerOpacity(id, 0.4)` → model, LayerMapData, save, and reopen readback
all show `0.4`. Full suite 16/16 green; ProjectStorage 153/153.
