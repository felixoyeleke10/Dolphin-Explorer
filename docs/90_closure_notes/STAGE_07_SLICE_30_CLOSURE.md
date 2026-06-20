# Stage 07 Slice 30 — DisplayStateManager (slice 3: global map palette ownership)

## Goal
Consolidate the **global map sonar palette** (one colour map for the whole map view)
into `DisplayStateManager` so its ownership/persistence + change notification go through
the single display bus — matching the slice-1 treatment of map quality.

Scope was confirmed by the user: **"Keep global map palette"** — one palette for the
whole map view (current behaviour). This is consolidation only, **no behaviour change**:
the palette is still global, not per-layer.

## What changed
- **`DisplayStateManager`** now owns the global map palette:
  - `mapPalette()` / `setMapPalette(int)` — setter persists `"sss/paletteIdx"` and emits
    `displayStateChanged({}, Palette)` (empty layer_id = global/per-view).
  - `initMapPalette(int)` — seeds the value without persist/emit, used once at startup to
    mirror the controller's computed initial palette (the controller keeps the
    app-default-name fallback for a fresh install).
  - `int m_map_palette = 1;` member (PaletteIndex::Greyscale).
- **`SidescanViewController::setPaletteIndex`** is now apply-only — it no longer writes
  QSettings (the manager owns persistence). Added `int paletteIndex() const` getter so
  MainWindow can seed the manager at startup.
- **`MainWindow.WaterfallCoordinator::onPaletteChanged`** routes the map-apply through
  `m_display_state->setMapPalette(idx)` instead of calling the controller directly.
- **`MainWindow.cpp`**: the `displayStateChanged` handler now applies
  `Palette + empty layer_id → m_sss_ctrl->setPaletteIndex(m_display_state->mapPalette())`.
  Startup seeds `m_display_state->initMapPalette(m_sss_ctrl->paletteIndex())` next to the
  map-quality apply.

## No-loop note
The bus handler calls `setPaletteIndex` (apply-only), not `onPaletteChanged`, so changing
the palette does not re-enter `setMapPalette`. `setMapPalette` early-returns on an equal
value as a second guard.

## Build
`dolphin-ui-systems`, `dolphin-ui-map`, `dolphin-ui-mainwindow` compile + link clean.

## Runtime verification (manual — UI can't be auto-tested)
- Change the map palette → the whole map recolours.
- Reopen the project → the chosen palette persists.

## Remaining DSM migration (next slices)
2. SBP gain/signal — SubBottom window through DSM.
3. Nav overlay choices — through DSM (NavOverlay).
4. Per-view: 3D settings + waterfall view params owned by DSM (ThreeD/WaterfallView).
5. Finish global-default bridging (background, coord format) so all views read the DSM.
