# Stage 07 Slice 05 Closure — Per-Modality Navigation/Geometry + SBP Nav Backend

## Goal
Navigation and Geometry were universal right-panel sections (always shown under
the Properties tab) but are functionally sensor-specific. Move them under the
per-modality sensor tabs so each modality gets its own instance, and build the
sub-bottom (SBP) nav-correction backend so the SBP versions are fully functional
— not stubs.

## Part 1 — Per-modality Navigation/Geometry (UI)

Previously `RightPanelHost(UniversalOnly)` created Info + Navigation + Geometry,
shown under the Properties tab for every layer. Now:

- `NavigationModule` / `GeometryModule` take a `primaryModality` (Sidescan or
  SubBottom) and report it via `primaryModality()`.
- `RightPanelHost(ModalOnly)` creates one Navigation + one Geometry per modality
  (`m_navigation_sss/_sbp`, `m_geometry_sss/_sbp`). The existing modality filter
  shows each under its own sensor tab — no filter-logic change needed.
- `UniversalOnly` now hosts only Info.
- Accessors became modality-keyed: `navPanel(Modality)` / `headingPanel(Modality)`.
- `MainWindow` keeps SSS pointers (`m_nav_panel`, `m_heading_panel`) and adds SBP
  pointers (`m_sbp_nav_panel`, `m_sbp_heading_panel`), all sourced from the modal
  host in `buildPropertiesPanel`.

## Part 2 — SBP nav-correction backend

SBP had no nav-correction path (`SubBottomWindow` couldn't apply smoothing/layback).
New end-to-end pipeline mirroring the sidescan display-time model:

- **`SbpNavCorrection.{h,cpp}`** (new) — `applySbpNavCorrections(traces, params)`:
  gap-aware GPS position smoothing, towfish layback (offsets each position
  backward along the resolved travel bearing via `geo::offsetNavByGroundMetres`),
  and a constant heading offset. No-op when nothing is enabled.
- **`SubBottomWindow`** — stores `m_nav_params`; `applyNavToLine()` re-runs the
  processing pipeline, which now calls `applySbpNavCorrections` on the trace copy
  before the DSP passes (so the window's cursor read-out reflects corrected nav).
- **Map ribbon** — `MainWindow::buildSbpProfileMap()` (extracted from the inline
  `onLayerSelected` SBP branch) applies the layer's stored nav params to the
  traces before `buildSbpProfileMapData`, so the colored map ribbon plots at the
  corrected positions.
- **Orchestration** — `applySbpNavToLine` / `applySbpNavToAll` store params per
  layer in `m_layer_nav_params`, refresh the SBP window, and rebuild the affected
  map profile(s). Wired from the SBP Navigation/Geometry panels in
  `SubBottomCoordinator`.
- **Persistence (session)** — params live in `m_layer_nav_params` (keyed by layer
  id, shared with the SSS path) and are re-applied on SBP open via
  `applyStoredSbpNavParams`, which also clears stale params when switching to a
  line that has none. Mirrors the sidescan model (params re-applied on open, not
  baked into the `.dlpd`).

## Files Changed
New:
- `src/ui/features/subbottom/SbpNavCorrection.h` / `.cpp`

Modified:
- `src/ui/mainwindow/rightpanel/RightPanel.Navigation.h` — modality ctor + `primaryModality`
- `src/ui/mainwindow/rightpanel/RightPanel.Geometry.h` — modality ctor + `primaryModality`
- `src/ui/mainwindow/rightpanel/RightPanelHost.h` / `.cpp` — per-modality instances + keyed accessors
- `src/ui/mainwindow/MainWindow.h` — SBP panel pointers + SBP nav method decls
- `src/ui/mainwindow/MainWindow.MainArea.cpp` — source Nav/Geometry from modal host (SSS + SBP)
- `src/ui/features/subbottom/SubBottomWindow.h` — `applyNavToLine`, `m_nav_params`, include
- `src/ui/features/subbottom/SubBottomWindow.Processing.cpp` — apply nav in pipeline + apply method
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp` — `buildSbpProfileMap`,
  `applySbpNavToLine/All`, `applyStoredSbpNavParams`; SBP branch calls the helper
- `src/ui/mainwindow/coordinators/MainWindow.SubBottomCoordinator.cpp` — wire SBP Nav/Geometry
  panels + apply stored params on open
- `src/ui/CMakeLists.txt` — register `SbpNavCorrection.cpp`

## Notes / Follow-ups
- `NavProcessingParams.h` still lives under `ui/features/waterfall/` but is now
  included by the subbottom feature (header-only, no link dependency). A future
  cleanup could relocate it to a neutral home (e.g. `app/display/`).
- Nav corrections are session-scoped params re-applied on open, consistent with
  the sidescan model; cross-session persistence into the project file is not part
  of this slice.

## QC Refinements (post-implementation review)
- **Correction order** — heading offset is now applied *before* layback so the
  layback direction uses the corrected heading (was after).
- **Heading guard** — the heading offset only adjusts heading fields that are
  populated (0 == "not present"); it no longer fabricates a heading on traces
  that have none. Consistent across `heading_deg` / `sensor_heading_deg` /
  `ship_heading_deg`.
- **Layback CRS** — verified `geo::offsetNavByGroundMetres` handles both
  projected (adds metres) and geographic (cos-lat) nav, so layback is correct
  for UTM and WGS84.
- **Disambiguation** — Navigation/Geometry sections carry an `SSS` / `SBP`
  modality badge, so the (intentionally same-titled) per-modality sections are
  distinguishable when both are visible under the Map/overview tab.
- **Robustness** — `applySbpNavToLine` falls back to the active layer when the
  SBP window's current line is empty, so the map ribbon still updates.
- **Consistency note** — SBP right-panel actions (Navigation/Geometry, like the
  existing Gain/Signal) are wired when the SBP window is first opened; this
  matches the established SBP tool pattern.

## Build Result
All changed/new translation units compiled with **0 errors, 0 warnings**; every
static library and the final `DolphinExplorer.exe` relinked cleanly (verified
after closing the running instance).
