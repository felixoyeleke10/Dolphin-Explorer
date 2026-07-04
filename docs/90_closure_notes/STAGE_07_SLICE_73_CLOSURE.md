# Stage 07 — Slice 73: QC main-window vs SSS-waterfall tools; fix tool-toggle sync

## Request
"QC the tools in the main window and SSS waterfall window — I think there are some
disparities. Make sure they are working right."

## QC findings
- **Classifications consistent (OK):** Contact classes (Boulder/Debris/Cable/Pipeline/
  Anomaly/Unknown) and Feature classes (Unclassified/Debris Field/Survey Zone/Exclusion
  Zone/Cable Corridor/Pipeline/Boundary/Sand Waves) are identical in the main-window
  sections and the waterfall sections.
- **Both create project contacts/features** through the same commands; classification
  flows through correctly on both surfaces.
- **Disparity (fixed):** the main-window annotation tools weren't kept in sync with the
  active map tool, unlike the waterfall (which already resets + mutually-excludes its
  Contact/Feature tools). In the main window:
  - switching to Cursor/Select/Zoom/Measure left the Contact Picking / Feature Drawing
    section toggles still checked;
  - the two sections didn't turn each other off;
  - the toolbar Contact button and the Contact Picking section weren't synced.

## Fix
Added `MainWindow::syncAnnotationToggles(contact, feature)` — sets the toolbar Contact
button + both section toggles (signal-blocked) so exactly one annotation tool reads active,
cleared for non-annotation tools. Called from:
- `onToolCursor/Select/Zoom/Measure` → `(false,false)`;
- `onAddContact` → `(true,false)`; `onDrawFeature` → `(false,true)`;
- the section handlers (pickToggled / feature toolChanged) → matching state.
Now the main-window tools behave like the waterfall's: one active tool, toggles honest.

## Verification
- Build green; `ctest -E PerfBaseline` → 16/16.
- NEEDS VISUAL CHECK: toggle Contact Picking on → Feature Drawing + other tools clear and
  the toolbar Contact button lights; pick Feature → Contact clears; click Cursor → both
  section toggles clear.

## Files
- `src/ui/mainwindow/MainWindow.{h,Tools.cpp,MainArea.cpp}`

## Not changed (noted for later)
Main window groups SSS processing as Radiometry + Enhancement + Navigation + Geometry
sections; the waterfall uses Image Processing + Processing + Nav (+ Seabed Picking, which
is waterfall-only). These are different groupings of similar params with separate panel
classes — a deeper unification is out of scope here and left as future work.
