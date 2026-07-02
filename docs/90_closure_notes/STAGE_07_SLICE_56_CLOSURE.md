# Stage 07 — Slice 56: SBP per-line progress + remove dead per-section apply plumbing

## Request
"fix all, remember fix system level, not band aid" — complete the SBP side of the
batch processing view AND remove every remnant of the old per-section Apply path
(not just disconnect it). No band-aids.

## Changes

### 1. SBP per-line batch + progress (uniform with SSS)
- `MainWindow.LayerCoordinator.cpp::buildSbpProfileMap` gained a `report(pct, phase)`
  lambda that routes through `m_import_overlay->updateJob` for the line's card when it
  is in `m_tools_apply_layers` (marshalled to the UI thread). Ticks:
  15 % "Reading traces…", 70 % "Applying corrections…", 90 % "Building profile…".
- `on_finally` closes the card: `if (m_import_overlay && m_tools_apply_layers.erase(lid) > 0)
  m_import_overlay->finishJob(lid, tr("Tools applied"));`
- SBP and SSS now share the SAME tracking set (`m_tools_apply_layers`) and the SAME
  dialog card lifecycle — one batch view, two pipelines.

### 2. Dead per-section apply path fully removed (system-level)
Removed — not disconnected — across every right-panel module:
- `MainWindow.h`: `m_sss_apply_active`, `m_sbp_apply_active`, `onSssDisplayApplyLine/All`,
  `applySssCorrection` (gone in earlier sub-step; confirmed no references remain).
- `GainControlPanel.{h,cpp}`: removed `applyToLineRequested/applyToAllRequested` signals,
  `onApplyLine/onApplyAll` slots, Apply button members, and `buildParams()`. Section now
  edits values only and contributes via `writeInto(WaterfallParams&)`.
- `ImagingControlPanel.{h,cpp}`: same removal (signals/slots/buttons/`buildParams`).
- `NavInfoPanel.h` / `HeadingInfoPanel.h`: removed the now-unconnected
  `applyToLineRequested/applyToAllRequested` signal declarations; sections contribute via
  `writeInto(NavProcessingParams&)`.
- SBP gain/signal modules (`RightPanel.SbpGain`, `RightPanel.SbpSignal`): signals/slots/
  buttons removed; `SubBottomCoordinator` per-section apply connects removed.
- `MainWindow.MainArea.cpp`: all per-section apply connects removed; single bottom Apply
  bar (Apply to Line / Apply to All) is the only apply surface.

The waterfall window's own `applyToLineRequested/applyToAllRequested` (in
`WaterfallAnalysisPanel` / `WaterfallWindow`) are unrelated and intentionally kept.

## Files
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp`
- `src/ui/mainwindow/panels/GainControlPanel.{h,cpp}`
- `src/ui/mainwindow/panels/ImagingControlPanel.{h,cpp}`
- `src/ui/mainwindow/panels/NavInfoPanel.h`, `HeadingInfoPanel.h`
- `src/ui/mainwindow/rightpanel/RightPanel.SbpGain.*`, `RightPanel.SbpSignal.*`
- `src/ui/mainwindow/coordinators/MainWindow.SubBottomCoordinator.cpp`
- `src/ui/mainwindow/MainWindow.{h,MainArea.cpp}`

## Verification
- Build green (`cmake --build . --parallel` → links `DolphinExplorer.exe`).
- Full suite green: `ctest` → 17/17 passed.
- `grep applyToLineRequested|applyToAllRequested|onApplyLine|onApplyAll` over `src/ui`
  confirms only the waterfall window's own (intended) apply signals remain.
- NEEDS VISUAL CHECK: Apply to All on a project containing both SSS and SBP lines →
  every line gets a card; SSS cards advance through Reading/Applying/Georef/Mosaic,
  SBP cards advance Reading traces → Applying corrections → Building profile; header
  counts done.
