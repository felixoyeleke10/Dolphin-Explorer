# Stage 07 · Slice 78 — Map tool behaviour QC (input-path bugs)

## Goal
Deep QC of the tools themselves — the actual input behaviour in `MapViewInput`
(pan/select/zoom/measure/contact/feature), the keyboard shortcuts, and the
feature-commit paths in all three views. User report: "the tools have so many
bugs". Five real bugs found and fixed.

## Bugs found and fixed

### T19 — Single-letter shortcuts made V/S/Z/M/C untypeable app-wide (critical)
The tool shortcuts were bare-letter `QAction`s with `Qt::ApplicationShortcut`.
Qt's shortcut map consumes matching keys **before the focused widget sees
them**, so typing "v", "s", "z", "m" or "c" in ANY text field anywhere in the
app — search boxes, project names, the contact editor's Name/Description —
switched map tools instead of typing the letter.
**Fix**: replaced the shortcuts with `MainWindow::eventFilter` installed on the
application. It fires the tools only when there are no modifiers, no modal
dialog, no popup menu, and the focus widget is not accepting text
(QLineEdit/QTextEdit/QPlainTextEdit/QAbstractSpinBox/editable QComboBox).
Still app-wide (works from the viewer windows), but never steals typing.

### T5 — Select tool: a plain click did nothing
`mouseReleaseEvent`'s ModeSelect branch only handled the rubber-band drag; a
simple click with the Select tool selected nothing (only ModePan clicks
hit-tested). **Fix**: extracted the click-select logic (hit test, Ctrl-toggle,
empty-space clears) into a shared lambda used by both Pan and Select clicks.

### T3 — Double-click commit added a duplicate final vertex (all 3 views)
A double-click commit arrives as press (adds a vertex) + dblclick (commits), so
every polygon/line committed by double-click carried a near-duplicate final
vertex — and 1 real vertex + the duplicate passed the 2-point minimum, letting
degenerate zero-length lines into the project. Qt allows a few px of slop
between the two presses, so exact equality isn't enough.
**Fix**: commit now strips trailing vertices within 6 px (pixel-space compare)
before the minimum-vertex check — click tools only; pen points are legitimately
close and never commit via double-click. Applied in `MapView::commitFeatureDraft`
and new shared `commitFeatureDraft()` helpers in `WaterfallView` and
`SubBottomView` (double-click + Enter paths now share one commit routine).

### T1 — No way to pan while Measure / Contact / Feature tools active
Left-click is consumed by those tools and there was no middle-button pan, so
measuring or drawing across more than one screen required switching tools
(losing the measurement/draft). **Fix**: middle-button drag now pans in every
mode; release restores the mode's cursor.

### T10 — Esc didn't clear a measurement
Right-click and double-click cleared it, Esc did not — and Measure mode never
took keyboard focus so key events couldn't arrive. **Fix**: Measure mode takes
focus like feature drawing; Esc clears the measurement.

### Bonus — right-click cancel leaked pen state (waterfall + SBP)
`contextMenuEvent` draft-cancel cleared the points but not `m_feature_pen_down`
(same class as the slice-77 Escape fix). Both views now reset it.

### Round 2 (follow-up "go ahead")
- **Cursor lost after every tool click** — the left-release handler reset the
  cursor to `Select ? Cross : OpenHand`, so Measure/Contact/Draw lost their
  crosshair after the first click. All cursor choices now flow through one
  `cursorForMode()` helper used by `setInputMode` and both release paths.
- **Zoom tool cursor** — now a magnifier (built from the toolbar `zoom.svg`
  glyph, hotspot on the lens) instead of `SizeAllCursor`.
- **Rubber-band select is exact** — `layersInRect` now tests true polygon∩rect
  (`QPolygonF::intersects`) after the bbox pre-reject, so a diagonal line whose
  bounding box crosses the band no longer gets selected when the ribbon itself
  doesn't touch it.

## Verification
Full build green (MSVC + Ninja); `ctest -E PerfBaseline` → 16/16 passed.

## Notes
- Manual checks worth doing on next run: type "v/s/z/m/c" in the project search
  box and contact editor (letters must appear); Select-tool click a line;
  double-click-finish a 2-click line (must NOT create it — degenerate);
  middle-drag while measuring.
- (Former cosmetic debt — Zoom cursor and bbox-only rubber-band select — fixed
  in Round 2 above.)
