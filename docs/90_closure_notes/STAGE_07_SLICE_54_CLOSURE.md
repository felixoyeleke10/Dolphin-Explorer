# Stage 07 — Slice 54: One shared Apply bar for the right-panel tools

## Goal
Replace the per-section "Apply to Line / Apply to All" buttons (one pair under every
tool section — gain, imaging, nav, geometry, SBP gain/signal) with a SINGLE Apply bar
pinned at the bottom of the tools panel that applies everything in one rebuild.

## Change
- **Panels** keep their sections/controls but lose their Apply buttons. Each now
  exposes a getter so the bar can gather settings:
  - `GainControlPanel::writeInto(WaterfallParams&)`, `ImagingControlPanel::writeInto(...)`
  - `NavInfoPanel::writeInto(NavProcessingParams&)`, `HeadingInfoPanel::writeInto(...)`
  - SBP `SbpGainModule`/`SbpSignalModule` already had public `currentParams()`.
  (Their old apply signals/slots are left in place but unused — harmless; nothing
  emits them now.)
- **Bottom Apply bar** (`MainWindow.MainArea.cpp`): a single Apply-to-Line / Apply-to-
  All pair in the lower sensor shell, below the scrolling sections. Shown only when an
  SSS/SBP layer is active (`updateToolsApplyBar`, called from onLayerSelected /
  refreshInspectorModalities) — no dead bar for Map/raster.
- **Coordinated apply** (`MainWindow::applyActiveTools`):
  - SSS: gather gain+imaging → one `WaterfallParams`, nav+attitude → one
    `NavProcessingParams`, store on the target layer(s), sync the live waterfall, then
    a SINGLE map rebuild (`reloadLayer`/`reloadCurrentLayer`).
  - SBP: gather gain/signal/nav, store display state, push live to the SBP window, and
    rebuild the profile via `applySbpNavTo*`.

## Why
Four+ identical button pairs were noise, and each fired its own map rebuild (tweak gain
+ ARN = two rebuilds). One bar = one gather + one rebuild, matching how the map build
already applies nav + the full amplitude chain in a single pass.

## Files
- `src/ui/mainwindow/panels/GainControlPanel.{h,cpp}`, `ImagingControlPanel.{h,cpp}`,
  `NavInfoPanel.{h,cpp}`, `HeadingInfoPanel.{h,cpp}`
- `src/ui/mainwindow/rightpanel/RightPanel.SbpGain.cpp`, `RightPanel.SbpSignal.cpp`
- `src/ui/mainwindow/MainWindow.{h,MainArea.cpp}`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp`

## Verification
- Build green.
- NEEDS VISUAL CHECK: select an SSS layer → sections have no per-section Apply; one
  Apply to Line / Apply to All sits at the bottom; adjusting gain + ARN + nav then
  Apply updates the map in one pass. Select an SBP layer → bar applies SBP tools.
  Map/raster layer → bar hidden.

## Processing dialog routing (SSS + SBP)
The bottom Apply routes through the execution/processing dialog in **Processing** mode
(job format `"COR"`/`"SBP"` → "Processing N line(s)", not "Importing"):
- **SSS**: `applyActiveTools` opens the `sss_apply` job, rebuilds via
  `applyLiveCorrections` (→ `prebuildTier`); the existing `loadingProgress` /
  `prebuildTierFinished` handlers drive + close it.
- **SBP**: opens the `sbp_apply` job; the SBP profile rebuild (`buildSbpProfileMap`,
  run off-thread via OperationManager) got an `on_finally` that closes the job when an
  SBP Apply is active (`m_sbp_apply_active`).

## Informative processing dialog
The job card describes the work instead of a bare "Reading N%":
- Label = scope (line name / "all lines") + the enabled corrections
  (e.g. "Line_023 — TVG, ARC, ARN, Slant Range, Nav").
- Status cycles through phases via a new `updateJob(id, pct, status)` overload, mapped
  from the build's progress: Reading pings → Applying corrections → Georeferencing →
  Building mosaic → Finishing (with %). SBP shows "Building sub-bottom profile…".

## Note
The old per-section apply signal wiring (gain/imaging → onSssDisplayApply*, nav →
onWaterfallNavProcessLine, SBP module signals) is now dead but retained. A later
cleanup can remove those signals/slots/connects entirely.
