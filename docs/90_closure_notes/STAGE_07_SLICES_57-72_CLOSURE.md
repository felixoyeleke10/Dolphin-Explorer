# Stage 07 — Slices 57–72: Feature Picking + Viewer Consistency (consolidated)

Consolidates the per-slice notes 57–72 into one record. Four themes:
(A) Feature Picking, (B) Viewer chrome consistency, (C) Prev/Next line navigation,
(D) QC fixes + line-open UX. All slices built green; `ctest -E PerfBaseline` = 16/16
throughout (full suite 17/17 where GlSmoke ran). Behavioral items were verified by build
+ automated tests and flagged NEEDS VISUAL CHECK (no GUI run in this environment).

---

## A. Feature Picking (new subsystem) — slices 57–63

Features are SHAPE annotations (polygon/polyline), a sibling of point-pick Contacts;
never conflated in the model or UI. Decisions: **map-first**, geometry **Polygon +
Polyline**, stored as **geo vertices** (unified model → render on map, waterfall, SBP).

- **Model + storage + serialization (57):** `core::Feature` (`FeatureType{Polyline,
  Polygon}`, `GeoVertex{lat,lon}`, classification, notes, tags, group_id, `line_id`).
  `Project` gained `addFeature/updateFeature/removeFeature/features()` +
  `featureAdded/Removed/Updated`; `Project.Features.cpp` (mirrors Project.Contacts.cpp);
  `.dlp` `features[]` read/write. Test: `testFeaturePersistence`.
- **Map drawing + render + undo (58):** `MapView::ModeDrawFeature` (click adds vertex,
  dbl-click/Enter commit, Esc/right-click cancel, Backspace undo) → `featureDrawn`;
  teal render in `MapViewPaint.Features.cpp`; `AddFeatureCommand`; feature signals routed
  via `ProjectEventBus` → map repaint.
- **Left-panel list (59):** `LineListPanel` FEATURES section (browse/select→highlight/
  remove via `RemoveFeatureCommand`); classify-on-draw presets.
- **Waterfall feature drawing (61):** `WaterfallView::setFeatureTool` reusing the shared
  `rangeToGeo` (also used by contact pick); FEATURE PICKING panel section made real;
  `featureDrawn`→`WaterfallWindow::featureCreated`→`onWaterfallFeatureCreated`.
- **SBP contact + feature picking (62):** `SubBottomView` contact/feature tools
  (`traceGeoAt` → geo from the clicked trace's nav); toolbar tools (later removed, see
  below); `onSbpContactCreated` / reuse `onWaterfallFeatureCreated`.
- **REDO as panel tool-sections (60→63):** user directive — Contact Picking + Feature
  Drawing must be **collapsible tool sections** (like Radiometry/Enhancement/Navigation/
  Geometry), not toolbar buttons/menus. Reusable widgets `ui/shared/panels/
  ContactPickingPanel` + `FeatureDrawingPanel`; wrapped as `IRightPanelModule`s in the
  main-window ModalOnly `RightPanelHost` (Unknown-primary modules treated as universal →
  shown on every tab); same widgets as `CollapsibleSection`s in the SBP viewer; the
  waterfall keeps its own inline sections. Removed the map toolbar Feature button + `F`
  shortcut + context-menu "Draw Feature", and the SBP toolbar Contact/Feature buttons.
  (Slice 60 = the toolbar version that was superseded by 63.)

**Coverage (complete):** Map · Waterfall (SSS) · Seabed (SBP) each have Contact ✅
Feature ✅ as panel sections.

**Deferred:** committed features/contacts show on the map + lists but are not re-drawn
back onto the waterfall/SBP image (inverse geo→trace/ping); rename/re-classify from the
list; feature groups/tags; SBP contact thumbnails.

---

## B. Viewer chrome consistency — slices 64, 67

Both viewers share `ViewerToolbar` (5 left buttons + centred command pill + right tools).
- **64:** command bar wrapped in the main window's `QFrame#uniBar` "blue ribbon" pill
  (`titleSearch` QSS, accent-blue on focus) and centred via a 3-column grid so SSS and
  SBP line up identically and match the main window.
- **67:** removed the post-show resize hook that sized the pill after show (caused the
  SBP search bar to flash off-centre then snap in). Width is now layout-driven (column
  stretch 36:kCmdBarPct:36 + pill minimum width) — correct on the first paint.

---

## C. Prev/Next line navigation — slices 65, 66, 69, 70, 71

- **65:** Prev/Next actually reloads the open viewer (`on*Open` after `onLayerSelected`);
  waterfall filters to sidescan lines (was `artifactCount()`).
- **66:** Prev/Next disabled at the ends via shared `ui/shared/LineNavigation.h`
  `computeLineNav`; inspectors gained `setNavEnabled`, windows `setLineNavEnabled`.
- **69:** the viewer's **own loaded line is the source of truth** — open resolves a valid
  line of the viewer's modality (active if matching, else first), and Prev/Next +
  enablement use `m_*_win->currentLayerId()` (not the app active layer); fallback made
  consistent across SSS/SBP.
- **70:** `onBottomTrack()` no longer refuses to open the SBP viewer when the active layer
  isn't SBP — it just calls `onSubBottomOpen()` (which guards + selects a valid line).
- **71:** Prev/Next enablement + navigation use the **same criterion as the visible
  LINES/FILES list — modality** — so the buttons reflect the lines the user sees
  (previously `index_built && count>0` could disable both while the list showed several).

---

## D. QC fixes + line-open UX — slices 68, 72

- **68 (QC):** SSS Apply-All guarded to sidescan layers (no longer writes SRC/SSS display
  onto SBP/raster); contact on absolute row 0 keeps its waterfall identity (discriminate
  by `range_m>0`, not `artifact_id>0`); feature draft dropped on line change in both
  viewers; SBP contacts store the clicked depth (`depth_s × half-sound-speed`); "Clear
  All Contacts" consistent (both route to `onClearContacts`, confirmed + undoable); SBP
  "Scroll to End" syncs the scrollbar.
- **72:** double-click a line to open its viewer from **both** the left panel (already
  wired) and the **mosaic** (new `MapView::layerActivated` → open by modality).

---

## Key files / entry points (for future work)
- Model: `src/core/Feature.h`; storage `src/app/project/Project.Features.cpp` + serialization.
- Shared UI: `src/ui/shared/panels/{ContactPickingPanel,FeatureDrawingPanel}.*`,
  `src/ui/shared/widgets/ViewerToolbar.*`, `src/ui/shared/LineNavigation.h`.
- Map: `src/ui/features/map/MapView*` (+ `paint/MapViewPaint.Features.cpp`).
- Waterfall: `src/ui/features/waterfall/WaterfallView*`, `WaterfallWindow*`.
- SBP: `src/ui/features/subbottom/SubBottomView*`, `SubBottomWindow*`.
- Coordinators: `MainWindow.{Waterfall,SubBottom}Coordinator.cpp`, `ContactCoordinator.cpp`.
- Memory: `project_feature_picking`, `feedback_viewer_consistency`.
