# Stage 07 Slice 29 — DisplayStateManager (slice 1: foundation + map-quality)

## Goal (user spec)
A display-state authority that owns global defaults, per-layer display state, and
per-view display state; persists project + globals cleanly; and emits
`displayStateChanged(layer_id, aspect)` so map / waterfall / SBP update consistently.
Design choice (confirmed): **coordinator over the model** — per-layer state stays on
`DataLayer` (already persisted in project JSON), the manager is the single mutate point
+ notifier; global defaults stay in `AppState` (bridged).

## What already existed (so this slice didn't redo it)
- Per-layer display state is modelled on `DataLayer` (palettes, `SssDisplayState` /
  `SbpDisplayState`, `nav_state`, `visible`, `slant_range_corrected`) and **persisted in
  the project JSON** (`Project.Serialization`). The "not persisted" comment on DataLayer
  was stale. Reopen already restores the look.
- Global appearance/data defaults live in `AppState` (default_palette, map_bg_color,
  coord_format, auto_stretch, sound_velocity).

## Slice 1 — built
New spine system `ui/systems/DisplayStateManager` (sibling of AppState / WindowRegistry /
ProjectEventBus):
- `DisplayAspect` enum (Palette/Gain/Channel/NavOverlay/Visibility · MapQuality/ThreeD/
  WaterfallView · DefaultPalette/Background/CoordFormat/DefaultGain).
- `displayStateChanged(layer_id, aspect)` — the single bus. `layer_id` empty = per-view
  or global.
- **Owns per-view map preview quality**: `mapQuality()/setMapQuality()` (persists to
  QSettings, emits MapQuality). Migrated the three scattered reads/writes
  (`MainWindow.cpp` startup, View menu, map context menu, `onMapSonarQuality`) to go
  through it — one source of truth; the `displayStateChanged` handler applies it to the
  controller and syncs both menus.
- **Bridges global defaults** from `AppState` (defaultPaletteChanged → DefaultPalette;
  settingsChanged → Background/CoordFormat) so consumers have one bus.
- **Per-layer mutate API** (coordinator): `setLayerVisible/setLayerSssPalette/
  setLayerSbpPalette` write the DataLayer field + emit; `notifyLayerChanged` for callers
  that mutate display structs directly. MainWindow marks the project dirty on any
  per-layer `displayStateChanged`.
- `setProject()` keeps it pointed at the open project for per-layer lookups.

`dolphin-ui-systems` + `dolphin-ui-mainwindow` compile + link clean.

## Staged migration (next slices) — route through the manager + listen to the bus
1. SSS palette / gain / contrast / channel — `SidescanViewController::setPaletteIndex/
   setDisplayParams` mutate through DSM per-layer; map + waterfall react to
   `displayStateChanged(Palette/Gain/Channel)` instead of direct calls.
2. SBP gain/signal — SubBottom window through DSM.
3. Nav overlay choices — through DSM (NavOverlay).
4. Per-view: 3D settings + waterfall view params owned by DSM (ThreeD/WaterfallView).
5. Finish global-default bridging (background, coord format) so all views read the DSM.

Each migration is one aspect, one consumer set, one closure note — no big-bang rewire.
