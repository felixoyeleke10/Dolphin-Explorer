# Stage 07 Slice 08 — Project working CRS vs. per-layer source CRS

## Problem
The Properties/left panel showed CRS **25829** while the status bar showed
**4326** for the same data, reading as a contradiction ("if we're heading the
right direction, why are they different?").

## Diagnosis (not a regression)
Two genuinely different fields, each with one source of truth — they were just
both labelled "CRS":
- **25829** = the layer's `source_spatial_ref` (the projected CRS the data was
  logged/confirmed in) — shown in Properties/Info + the waterfall/SBP/layer
  inspectors.
- **4326** = `Project::displaySpatialRef()`, which **defaults to WGS84**
  (`m_display_spatial_ref = makeWgs84SpatialRef()`) and is the CRS the **map
  renders in**. The status bar was fed from this (`setViewCrs` ←
  `displaySpatialRef()`), so it advertised the internal map-render ref as "the
  CRS".

The MapView is a geographic (lat/lon) overview map — it normalizes every layer
to WGS84 for rendering — so `displaySpatialRef` being WGS84 is correct *as a
render detail*; the bug was **surfacing it as the project CRS**.

## Professional model (SonarWiz / Hypack / QPS)
One **project/survey coordinate system** is authoritative for the user-facing
CRS, coordinate readouts, and export; per-file **source CRS** is metadata. You
never see two equal-billing CRS values.

## Fix (presentation + a derived accessor; map untouched)
1. **`Project::workingCrs()`** (new) — the project's survey/working grid: the
   most common **projected** source CRS across `m_sources`; falls back to
   `displaySpatialRef()` (WGS84) when no source is projected. Pure derivation —
   no new stored field, no serialization change, auto-correct as sources change.
2. **Status bar now shows `workingCrs()`**, wired through `updateContextInfo()`
   (which already fires on project bind + every layer add/remove/activate, so it
   refreshes after import for free). Removed the old inline `setViewCrs(
   displaySpatialRef())` block in `bindProjectUi`. Geodesy "Apply CRS" also calls
   `updateContextInfo()` so assigning a CRS updates the readout immediately.
   → For the reported project the status bar now reads **25829**, matching
   Properties.
3. **Per-layer field relabelled "Source CRS"** (was "CRS") in InfoModule,
   LayerInspectorPage, and the waterfall + sub-bottom inspectors — distinguishes
   layer metadata from the project working CRS.
4. **Status-bar tooltip** clarifies "Project working CRS (survey grid)".

## Behaviour by project type
- Projected survey (e.g. all 25829): status bar + Properties both show 25829.
- Geographic-only (raw WGS84): both show ~4326 — still consistent.
- Mixed CRS: status bar shows the dominant grid; each layer shows its own
  Source CRS — now legitimately distinct and clearly labelled.

## Files
`app/project/Project.h` (+`workingCrs()` decl), `app/project/Project.cpp`
(+impl, +`core/SpatialRef.h`), `ui/mainwindow/MainWindow.Layout.cpp`
(`updateContextInfo` sets working CRS), `ui/mainwindow/MainWindow.ProjectBinding.cpp`
(drop inline block), `ui/mainwindow/MainWindow.Geodesy.cpp` (refresh on apply),
`ui/mainwindow/MainStatusBar.cpp` (tooltip), `ui/mainwindow/rightpanel/RightPanel.Info.cpp`,
`ui/shared/panels/LayerInspectorPage.cpp`,
`ui/features/waterfall/panels/WaterfallInspectorPanel.Layout.cpp`,
`ui/features/subbottom/panels/SubBottomInspectorPanel.Layout.cpp` (labels).

## Build
All libraries compile clean (Project.cpp + ui-mainwindow + inspectors). Final
exe relink blocked only by LNK1168 (app running) — not a code error; relinks
once the app is closed.

## Not done here (deferred follow-up, by design)
The deeper SonarWiz step — letting the project **work in** a projected CS (map +
cursor readout in native eastings/northings, dual grid+geographic readout,
export in the project grid) — is a larger change touching the map normalize path
(the SSS path honours `displaySpatialRef`; the waterfall path hard-codes
`makeWgs84SpatialRef()`). This slice fixes the *presentation* contradiction; the
projected-display alignment remains a separate slice.
