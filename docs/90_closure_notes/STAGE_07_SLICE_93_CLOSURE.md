# Stage 07 · Slice 93 — Right-panel diet: SBP Display + Contact Picking removed

## Goal (user direction)
"The SBP tools are too much — remove tools like Display and Contact Picking
from the right panel completely; move the necessary view-related display
tools under the Views tool for SBP."

## What changed

### Right panel (modal host)
- **SBP Display section removed** (SubBottomDisplayModule deleted:
  RightPanel.SubBottomDisplay.{h,cpp} gone from the tree and CMake, host
  members/accessors/signal removed — `setSbpParams`, `sbpParamsChanged`).
- **Contact Picking section removed completely** (ContactPickingModule +
  RightPanel.ContactPicking.h deleted; `contactPickingPanel()` accessor and
  all MainWindow wiring gone). Picking surfaces remain: the top-toolbar
  Contact tool (C) and the SSS/SBP viewer toolbars. The shared
  ContactPickingPanel widget class stays — the SBP viewer window still
  embeds it.
- The SBP tab now shows: Gain, Signal, Navigation, Geometry (+ shared Apply
  bar). SSS tab unchanged (Radiometry, Enhancement, Navigation, Geometry).

### Views ▸ SBP (left panel) — the display controls' new home
- Added **Gain** (0.1–20 ×), **Contrast** (0.5–3), **Invert polarity** next
  to the existing Palette — same ranges as the removed section. Live-apply:
  edits go straight to the active SBP line (SubBottomWindow refresh +
  persisted via DisplayStateManager::setLayerSbpDisplay). Populated from the
  active layer on selection/open (refreshViewsPanel), disabled with a hint
  when no SBP line is active.

### Rewired consumers
- SubBottomCoordinator: dropped Display-module syncs (open-restore and the
  Settings-dialog apply path); viewer-open now refreshes the Views panel.
- onPaletteChanged and LayerCoordinator open-restore: setSbpParams calls
  removed (Views mirrors via refreshViewsPanel).
- syncAnnotationToggles: no contact-panel check state to mirror any more.
- m_pending_contact_class now stays at its default; classification is set in
  the Contact Editor after picking.

## Round 2 — 2D/3D switch promoted to the main toolbar (user direction)
The "Toggle 3D View" entry moved out of the ··· overflow menu onto the main
(icon) toolbar as a checkable button, placed with the viewer shortcuts
(Geodesy / SBP / Waterfall / 3D). Checked = 3D; check state follows
MapViewportHost::modeChanged. Verified via toolbar grab.

## Round 3 — in-viewport 2D/3D switches removed entirely (user direction)
The toolbar button is now the SINGLE mode control:
- Floating corner "3D" button over the 2D map: removed (widget, overlay
  host, positionOverlay, QSS, kMap3DBtn theme tokens).
- In-HUD "2D" and "⊞ Terrain" chips in the 3D view: removed (hit-tests,
  hover cursor, switchTo2DRequested + loadTerrainRequested signals,
  promptLoadTerrain and terrainFileLoaded plumbing). The "Loading terrain…"
  feedback chip remains. The 3D context menu's "Load Terrain…" is now "Set
  Draping Surface…" → onChooseDrapingSurface (persists with the project,
  same flow as Views ▸ MAP).

## Verification
Build green; 16/16 tests. In-app grabs with the SBP project active:
right-panel SBP tab shows Gain/Signal only above the Apply bar (no Display,
no Contact Picking anywhere); Views ▸ SBP shows Palette (Inverted Grey from
the layer), Gain 1.0×, Contrast 1.0, Invert — enabled and populated.
