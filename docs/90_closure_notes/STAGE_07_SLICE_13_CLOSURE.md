# Stage 07 Slice 13 — Nav/Geometry Apply works from the main view (wiring fix)

## Root cause (reported)
The main-window SSS **and** SBP Navigation / Geometry panels are created during UI
setup, but their Apply buttons were only `connect()`-ed inside
`onWaterfallOpen()` / `onSubBottomOpen()`. With the viewer window never opened,
clicking Apply in the map/main view emitted into the void — the buttons did
nothing.

## Fix (Phase 1 — the wiring)
Moved the Nav/Geometry panel connections to **construction time**
(`MainWindow.MainArea.cpp`, the documented "signal wiring" spot, right after the
panel pointers are pulled), routing to model-owned MainWindow slots:
- SSS: `applyToLineRequested → onWaterfallNavProcessLine`,
  `applyToAllRequested → onWaterfallNavProcessAllLines`.
- SBP: `applyToLineRequested → applySbpNavToLine`,
  `applyToAllRequested → applySbpNavToAll`.

Removed those connections from `onWaterfallOpen()` (WaterfallCoordinator) and
`onSubBottomOpen()` (SubBottomCoordinator).

**Secondary issue fixed:** SSS apply-to-all previously routed through
`WaterfallWindow::applyNavToAll()` (live waterfall processing first, then emit
back to MainWindow) — backwards for a main-window control. It now goes straight to
the model-owned `onWaterfallNavProcessAllLines` (store first; refresh viewer if
open). The now-dead `WaterfallWindow::applyNavToAll` was removed.
`navProcessAllLinesRequested` is **kept** — the waterfall's own analysis panel
still emits it.

## Behaviour after Phase 1
- Buttons work from the main/map view whether or not a viewer is open.
- Apply → stores on `DataLayer::nav_state` + `markProjectDirty()` (persists; never
  bakes `.dlpd`).
- Viewer refreshed live only if open.
- **SBP is now fully correct**, including the map: `applySbpNavToLine/All` already
  rebuild the SBP profile via `buildSbpProfileMap` (which applies
  `applySbpNavCorrections`), even with the SBP window closed.

## Build
`dolphin-ui-mainwindow` + `dolphin-ui-waterfall` compile clean. Exe relink blocked
only by LNK1168 (app running).

## Phase 2 — SSS map now applies nav_state (SonarWiz single-nav-model) ✅
Done the professional way: SonarWiz / SeaView compute navigation **once** and every
renderer consumes the same corrected nav. The canonical SSS correction already
existed as the waterfall's `runNavCorrections` (layback = `GeoCorrectNode`,
smoothing = `NavSmoothNode`, plus attitude offsets) — so it was **lifted into the
app layer**, not reimplemented:
- New `app/display/NavCorrection.{h,cpp}`: `applySidescanNavCorrections(pings,
  params)` — the single SSS nav correction (only needs pipeline + core, both below
  app; fast no-op path when nothing is enabled).
- `WaterfallView::runNavCorrections` now **delegates** to it (NavCorrections test
  still passes → faithful lift).
- `SidescanViewController::activateLayer` applies it to the source pings before
  normalize, so the map reflects the **same** correction as the waterfall.
- `onWaterfallNavProcessLine` reloads the layer's map preview (guarded by
  `m_map_view->layerData(id)`, like SBP); `onWaterfallNavProcessAllLines` calls
  `reloadCurrentLayer()` to rebuild all loaded SSS layers. Corrections are applied
  at load time from `nav_state` — never baked into `.dlpd`.

Result: SSS Apply from the main view → stores on the layer + rebuilds the map with
corrected swaths + refreshes the waterfall if open. Map and waterfall agree by
construction.

## Build / tests
Full build green; `DolphinExplorer.exe` relinked; **ctest 13/13 pass** (NavCorrections
guards the lift; the rest unaffected).

## Follow-up — stale viewer-layer targeting fixed
Once the panels became main-view (window-independent), the apply-to-line handlers
still targeted the **viewer's** `currentLayerId()` first — stale when the map/tree
selection moved on, and empty (no-op) when the viewer was closed. Flipped every
main-view apply handler to target the **active** layer (which the panel reflects),
falling back to the viewer only when nothing is selected:
- `onWaterfallNavProcessLine` (SSS nav), `applySbpNavToLine` (SBP nav),
- SBP gain + signal apply-to-line (these had no active fallback at all → were also
  dead when the SBP window was closed),
- the SBP apply-to-all companion-param fetches (gain↔signal) now read the active
  layer, not the window's current line.
(The Display module already used `activeLayerId()`.) Build green; key tests pass.

## Follow-up — nav-correction consolidation + dead-code removal ✅
Investigated the deferred "converge SBP" item and found the literal direction was
wrong: `GeoCorrectNode`/`NavSmoothNode` are **SidescanPing-only** and **naive** (no
gap-awareness, heading-only layback), while SBP's `applySbpNavCorrections` is
**gap-aware with COG-fallback layback** — i.e. the *better* algorithm. Routing SBP
through the nodes would be a downgrade, so that convergence was correctly *not*
done. Instead:
- **Co-located** `applySbpNavCorrections` into `app/display/NavCorrection`
  (alongside `applySidescanNavCorrections`) — one app-layer module now owns both
  modalities' display nav corrections. This also **removed a cross-feature
  dependency** (`ui-mainwindow`/`ui-subbottom` no longer reach into each other for
  nav) — deleted `ui/features/subbottom/SbpNavCorrection.{h,cpp}`. No algorithm
  change.
- **Deleted dead `geo::NavSmoother`** (Kalman; zero consumers) + its CMake entry.

Build green; exe relinked; ctest 13/13.

## Still deferred (genuinely larger, optional)
Truly unifying the *algorithm* (one implementation for SSS + SBP) would mean
upgrading the SSS `GeoCorrect`/`NavSmooth` **nodes** to be gap-aware + COG-fallback
(matching SBP) — but they're registered DAG node types, so that's an
engine-/bake-level change deserving its own tested slice, not a bolt-on.
