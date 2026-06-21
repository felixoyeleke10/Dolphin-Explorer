# Stage 07 — Slice 48: Execution window on SSS gain/imaging Apply

## Goal
When the user clicks Apply on the right-panel SSS gain/imaging tools, the execution
progress window should come up (like import/bake) and stay until the map rebuild —
including the slow background tier prebuild — completes.

## Change
`MainWindow::applySssCorrection` now drives the `ExecutionProgressDialog`
(`m_import_overlay`, which auto-opens on `addJob`):

- On Apply: `addJob("sss_apply", …)` → the window appears; scoped by a new
  `m_sss_apply_active` flag so it only shows for an explicit Apply, not for ordinary
  layer selection. `m_sss_apply_staged` records whether the active quality tier
  (Medium/High) stages a background prebuild.
- Progress: the controller's `loadingProgress` updates the job bar. The first-paint
  build already reported 0→100; the **prebuild now reports too** (new coarse
  `report()` ticks: decoded 55 → corrected 70 → coverage 82 → raster 98), so the bar
  moves through the slow part instead of sitting idle.
- Close: `loadingFinished` closes it for non-staged tiers (Coverage/Low — first paint
  is the whole job); for staged tiers the window closes on the new
  `prebuildTierFinished` signal (emitted from the prebuild's `on_finally`, so it fires
  on success / failure / cancel — no stuck window).

New on `SidescanViewController`:
- `MapSonarQuality currentMapQuality() const` getter.
- `prebuildTierFinished(layer_id, quality)` signal (every outcome; complements the
  success-only `prebuildTierComplete`).
- Prebuild worker captures `this` and emits coarse `loadingProgress`.

Left untouched: the prebuild does NOT emit `loadingStarted/loadingFinished` (those are
balanced against the import overlay's map-load counter via `onMapLoadDone`); only the
side-effect-free `loadingProgress` + the new `prebuildTierFinished` are used.

## Files
- `src/ui/features/map/sidescan/SidescanViewController.h`
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp`
- `src/ui/mainwindow/MainWindow.h` / `MainWindow.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`

## Verification
- Build green.
- NEEDS VISUAL CHECK: Apply a tool → execution window appears, bar advances through
  decode/correct/raster, closes when the (staged) tier finishes. Confirm it does NOT
  pop up on plain layer selection.

## Note
For "Apply to All" the window closes on the first layer's `prebuildTierFinished`
(others continue in the background) — acceptable; the window's purpose (show the work
is happening) is met. A future refinement could ref-count all triggered builds.
