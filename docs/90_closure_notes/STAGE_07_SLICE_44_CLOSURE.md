# Stage 07 — Slice 44: Right-panel SSS gain/imaging tools dead from the map view

## Symptom
In the right panel's SSS tool area, the **Apply to Line / Apply to All** buttons in
the **Gain** (TVG / AGC / ARC) and **Imaging** (ARN / Destripe / Beam Pattern /
ML Enhance / SRC) sections did nothing — "not working at all" — unless the
waterfall window had already been opened in the session.

## Root cause
The gain and imaging panels are stable singletons owned by the modal `RightPanelHost`,
but their Apply signals were `connect()`-ed **only inside `onWaterfallOpen()`**, and
routed straight to `m_waterfall_win->applyExternalParams[ToAll]`. From the map view —
the waterfall window never opened (or closed) — the signals had no receiver, so every
tool in that area was inert. (Nav / Geometry had already been moved to construction-time,
model-owned wiring for exactly this reason; gain/imaging were left behind.)

## Fix
Mirror the Nav/Geometry pattern: wire gain/imaging at construction to model-owned slots.

- **`MainWindow::onSssDisplayApplyLine/All`** + shared **`applySssDisplayState(params, all_lines)`**
  (`MainWindow.WaterfallCoordinator.Processing.cpp`):
  - If the waterfall window is open → route through `applyExternalParams[ToAll]`
    (its in-session reprocessing + `paramsApplied` path re-syncs display state + map).
  - If closed → store params on the active layer (line) or every sidescan layer (all)
    via `DisplayStateManager::setLayerSssDisplay` (marks dirty), then refresh the SSS
    map live (palette + gain/contrast; identity stretch — full TVG/ARC/AGC still need
    a Bake, per existing policy). Logs an `ActivityKind::DisplayParams` entry.
- **`MainWindow.MainArea.cpp`** — connect `m_gain_panel` / `m_imaging_panel`
  `applyToLineRequested` / `applyToAllRequested` to the new slots at construction.
- **`MainWindow.WaterfallCoordinator.cpp`** — removed the old waterfall-only connects
  from `onWaterfallOpen()` (the model-owned slot now handles the open case), preventing
  double-application.

Apply remains display-state only; committing full corrections into `.dlpd` is still the
explicit Processing → "Bake Corrections into Data" command.

## Files
- `src/ui/mainwindow/MainWindow.h`
- `src/ui/mainwindow/MainWindow.MainArea.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`

## Verification
- Full MSVC/Ninja build green.
- Gain/imaging Apply now fires from the map view (waterfall closed) and stays correct
  when the waterfall is open (single application via the window's live path).

## Note
When the waterfall is closed, applying corrections that the map mosaic can't render
live (TVG/ARC/AGC) updates display state + persists but won't visibly change the map
until a Bake — by design (map renders DLPD amplitudes). Palette/gain/contrast/threshold
do change live. A future slice could surface a subtle "applied — bake to see on map"
hint to make that explicit.
