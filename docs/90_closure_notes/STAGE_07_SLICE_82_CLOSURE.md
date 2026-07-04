# Stage 07 · Slice 82 — Feature drawing tools moved to the main toolbar

## Goal (user direction)
Polygon / Line / Pen belong on the icon toolbar with the other interactive
tools — not in panel sections. Delete the Feature Drawing sections everywhere
(main window right panel, SSS waterfall, SBP viewer): feature drawing is a
map-first workflow and one surface is enough.

## What changed

### Main toolbar
- Three new checkable buttons (draw_polygon / draw_line / draw_pen icons) in
  the **TOP main toolbar** (after the Geodesy/SBP/SSS viewer shortcuts, own
  separator) — user correction: first cut put them on the right nav rail;
  "app menu" meant the top icon bar. Verified live via screenshot.
- They join the **same exclusive QButtonGroup** as the side nav tools (the
  group is window-parented and lazily created by whichever bar builds first) —
  exactly one interactive tool active at a time app-wide. Clicking one calls
  `onDrawFeature(kind)`.
- `syncAnnotationToggles` simplified: with every tool represented by a group
  button, checking the active tool's button is sufficient — the
  exclusivity-lift workaround from slice 77 is gone (it existed only because
  feature mode had no toolbar representation).
- No-project guard on `onDrawFeature` falls back to `onToolCursor()` (the
  raw click pre-checks the button; the UI must not claim an inactive mode).

### Removed surfaces
- **Right panel**: `FeatureDrawingModule` deleted
  (`RightPanel.FeatureDrawing.h` removed); host accessor + registration +
  MainArea wiring gone.
- **SBP viewer**: Feature Drawing collapsible section, panel member, and all
  wiring removed.
- **SSS waterfall**: `buildFeatureSection`, `setFeatureToolActive`,
  `featureToolChanged`, and the three tool-button members removed from the
  analysis panel; window-side connects removed.
- View-level feature code (map + waterfall + SBP `setFeatureTool`, pen
  handling, `featureDrawn` plumbing) stays in place — dormant in the viewers,
  active on the map via the toolbar. The shared `FeatureDrawingPanel` widget
  class itself remains in `ui/shared/panels` (unused; removable later).

## Round 3 — feature tools in the VIEWER toolbars too (user direction)
The same Polygon / Line / Pen buttons now sit in the **waterfall toolbar**
(between the contact-pick toggle and Edit Contact Details) and the **SBP
toolbar** (before Edit Contact Details) — reactivating the dormant view-level
drawing code:
- Checkable with manual exclusivity among the three; clicking the active tool
  again turns drawing off (viewer model — there is no Cursor button to return
  to).
- Mutually exclusive with contact picking and (waterfall) seabed tools in BOTH
  directions: the view setters enforce the state, the toolbar/panel toggles
  mirror it (`syncFeatureToolButtons` helper in each window; wired into
  contact/seabed activation paths and the line-change lifecycle reset).
- Fixed while wiring: `WaterfallView::setSeabedTool` cleared the feature draft
  but leaked `m_feature_pen_down` (same class as the slice-77/78 pen leaks).

## Round 4 — nav tools moved to the top toolbar too (user direction)
Cursor / Select / Zoom / Measure / Contact moved from the right vertical rail
into the TOP main toolbar (own separator, before the drawing tools), same
exclusive group — one interactive tool active app-wide. The right rail now
holds only the ··· overflow menu (3D toggle, reset nav, clear contacts).
Verified live via screenshot (Cursor checked state renders with accent tint).

## Round 5 — right rail retired (user confirmed)
`buildRightToolBar` deleted; the ··· overflow menu (Toggle 3D / Reset to Raw
Navigation / Clear All Contacts) and the Settings button moved to the RIGHT
END of the top toolbar behind an expanding spacer. The map canvas now runs to
the right window edge. Workspace-state functions that referenced the rail
(`setRightToolBarVisible` etc.) are null-guarded no-ops, so saved layouts
load cleanly. Verified live via screenshot.

## Verification
Full build green; `ctest -E PerfBaseline` → 16/16 passed.
