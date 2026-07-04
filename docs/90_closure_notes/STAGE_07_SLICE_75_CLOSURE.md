# Stage 07 · Slice 75 — Feature Drawing simplified to icon tools (Polygon / Line / Pen)

## Goal
The first feature-picking design exposed a "Draw Feature" toggle plus Type and
Classification combo boxes. That was too much ceremony for a drawing action.
Per product direction: **give the user tools — Polygon, Line, Pen — as icon
buttons. No draw toggle, no type combo, no classification. The geometry kind IS
the tool.** Consistent across the main map, the SSS waterfall viewer, and the
SBP viewer (SBP/SSS parity).

## What changed

### Tool model
- Tool ints unified everywhere: **0=none, 1=polygon, 2=line, 3=pen (freehand)**.
- Selecting a tool activates drawing directly; clicking the active tool again
  toggles it off. Manual single-select exclusivity (no radio group needed).

### Icons
- New 24×24 SVGs: `resources/icons/draw_polygon.svg`, `draw_line.svg`,
  `draw_pen.svg` (stroke `#aeaeb2`), registered in `resources/resources.qrc`.

### Shared panel
- `FeatureDrawingPanel` rewritten to three checkable icon `QToolButton`s
  emitting `toolChanged(int)` with `setActiveTool(int)` to reflect state.
  Removed classification/type/draw-toggle members and accessors.

### Map
- `MapView::setFeatureDrawKind(int)` replaces `setFeatureDrawPolygon(bool)`;
  `m_feature_kind` + `m_feature_pen_down`. Pen: press starts a fresh stroke,
  move appends while dragging (4px throttle), release commits as a polyline
  (`polygon=false`). Polygon/line keep click-to-add + double-click/Enter commit.
  Paint: pen draws only the polyline (no fill / live segment / vertex dots).
- `MainWindow::onDrawFeature(int)` + `syncAnnotationToggles(bool,int)`;
  classification dropped from `onFeatureDrawn`.

### Waterfall (SSS) viewer
- `WaterfallAnalysisPanel::buildFeatureSection` rebuilt to the same three icon
  buttons emitting `featureToolChanged(int)` with `setFeatureToolActive(int)`.
- `WaterfallView` pen freehand mirrors the map (press/move/release, throttle,
  double-click guarded for tool != 3, state reset on tool change/clear).

### SBP viewer
- `SubBottomView` pen freehand added for parity: `m_feature_pen_down`,
  press starts stroke, `mouseMoveEvent` appends while dragging via `traceGeoAt`,
  new `mouseReleaseEvent` commits `featureDrawn(pts, polygon=false, proj)`,
  `mouseDoubleClickEvent` guarded for tool != 3, `paintAnnotationDraft` strokes
  the pen path only, `setFeatureTool` resets pen state.
- `SubBottomWindow` wiring drops the classification member and passes an empty
  class string through the `featureDrawn` forward.

## Verification
- Full app build green (MSVC + Ninja).
- `ctest -E PerfBaseline` → 16/16 passed.

## Notes / debt
- The waterfall still uses its own thin feature section rather than embedding the
  shared `FeatureDrawingPanel` widget directly; behaviour is at parity. A future
  slice can collapse both to the single shared widget (deferred for GUI-test
  safety).
