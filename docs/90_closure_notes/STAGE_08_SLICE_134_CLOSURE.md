# Stage 08 Slice 134 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-134 — universal palette/display sync from every surface
- primary goal: a palette (or SBP display) change made in ANY window — main
  window, waterfall, or SBP viewer — reaches every other surface through the
  one DisplayStateManager authority

## What Was Broken

- **Waterfall → nothing.** The waterfall inspector's palette pick recolored
  only its own view, persisted a PRIVATE `waterfall/paletteIdx` QSettings key,
  and emitted `WaterfallWindow::paletteChanged` — which no one consumed. The
  map mosaic, right panel, and Views kept the old palette; reopening the
  waterfall restored the private key, so the two surfaces could disagree
  indefinitely. (A stale comment even claimed DisplayStateManager owned that
  key; it actually persists `sss/paletteIdx`.)
- **SBP window → nothing.** The SBP display panel's palette/gain/contrast/
  polarity edits updated only the profile view. The layer model
  (`sbp_palette`, `sbp_display_state`), the Views ▸ SBP page, the 3D curtains,
  and the project file never learned about them.

## What Changed

- `WaterfallWindow::paletteChanged` → `MainWindow::onPaletteChanged` (the slot
  the right panel already uses) → `DisplayStateManager::setMapPalette` → bus →
  map mosaic, right panel, Views, and back into the waterfall (loop-safe: its
  `setPalette` no-ops on an unchanged index).
- Retired `waterfall/paletteIdx`. All three readers (line-switch restore,
  layer-open restore, inspector construction) now read `sss/paletteIdx`, the
  key the authority persists — one key, one truth, no divergence on reopen.
- `SubBottomDisplayPanel` gained `userParamsEdited`, emitted ONLY on
  persist=true user actions (panel controls, bottom-track toolbar button) —
  never for programmatic sync. `SubBottomWindow` forwards it as
  `displayParamsEdited`; the SubBottom coordinator writes it to
  `DisplayStateManager::setLayerSbpDisplay` for the window's current layer,
  which updates `sbp_palette`, marks the project dirty, and emits the Palette
  aspect (curtain recolor + Views refresh ride the existing bus handler).
- `SubBottomWindow::applyDisplayParams` (the authority→window push) now uses
  `refreshParams()` (sync, no persist) instead of `notifyParamsChanged()`
  (user action) so a Views-originated edit cannot echo back into the
  authority as a second write.

## Files Touched

- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.SubBottomCoordinator.cpp`
- `src/ui/features/waterfall/WaterfallWindow.cpp`
- `src/ui/features/waterfall/WaterfallWindow.Lifecycle.cpp`
- `src/ui/features/waterfall/panels/WaterfallInspectorPanel.Layout.cpp`
- `src/ui/features/subbottom/SubBottomWindow.{h,cpp}`
- `src/ui/features/subbottom/panels/SubBottomDisplayPanel.{h,cpp}`

## Tests Or Validation

- Live end-to-end (temp diag, removed after):
  - simulated in-waterfall pick of palette 7 → DisplayStateManager 1→7,
    persisted `sss/paletteIdx` 1→7, map controller 1→7;
  - simulated in-SBP-window edit (palette 3, gain 4.5) via the real
    user-action path → layer `sbp_palette` 1→3, display gain 4.5,
    `display_customized` set, project dirty.
- Full rebuild clean; ctest 22/22 (PerfBaseline excluded, unaffected).

## Risks / Follow-Ups

- Views ▸ SSS per-line palette override remains a deliberate map-only
  override on top of the global palette (unchanged behavior).
