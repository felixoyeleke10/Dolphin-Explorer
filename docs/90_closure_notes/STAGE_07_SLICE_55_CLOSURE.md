# Stage 07 — Slice 55: Processing dialog — no blink + per-line batch view

## Symptoms / requests
1. The processing dialog popping on Apply made the (frameless) main window blink.
2. The dialog was uninformative — it should show the line it's working on and what it's
   doing on that line, plus the batch total / how many done / what's next.

## Fixes
### No blink
The dialog is a top-level over a frameless main window; grabbing the foreground made
the window drop/redraw. Fixed in `ExecutionProgressDialog`:
- `setAttribute(Qt::WA_ShowWithoutActivating)` — shows without stealing activation.
- `addJob` no longer calls `activateWindow()` (kept `raise()` only).

### Per-line batch view
The bottom-bar SSS Apply now creates ONE dialog card per line that will rebuild:
- `applyActiveTools` computes the target lines (active line for Apply-to-Line; every
  loaded SSS line for Apply-to-All, via new `SidescanViewController::loadedLayers()`),
  `setQueueTotal(N)`, and `addJob(layer_id, "<line> — TVG, ARC, ARN…")` per line.
- New signal `SidescanViewController::prebuildTierProgress(layer_id, pct)` (emitted from
  the tier-build report, carrying the layer id) updates the right card with a readable
  phase: Reading pings → Applying corrections → Georeferencing → Building mosaic →
  Finishing.
- `prebuildTierFinished(layer_id)` marks that card Done and removes it from the tracked
  set (`m_tools_apply_layers`).
- The dialog header shows "Processing N line(s)" + "X of Y" done; cards not yet started
  read "Waiting…" (the map lane runs 2 at a time, so the rest are what's next).

SBP keeps a single card ("<line> — Building sub-bottom profile…", closed by the
profile build's on_finally) — per-line SBP progress is a follow-up.

## Files
- `src/ui/features/import/ImportProgressDialog.cpp` / `.Jobs.cpp` / `.h`
- `src/ui/features/map/sidescan/SidescanViewController.h` (loadedLayers, prebuildTierProgress)
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp` (emit prebuildTierProgress)
- `src/ui/mainwindow/MainWindow.{h,cpp}` (m_tools_apply_layers + handlers)
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`

## Verification
- Build green.
- NEEDS VISUAL CHECK: Apply to All on a multi-line SSS project → dialog lists every
  line, ~2 advance through phases at a time, others read "Waiting…", header counts
  done; the main window no longer blinks when the dialog appears.

## Note
The old dead `applySssCorrection` / `onSssDisplayApply*` path (unused since the
per-section buttons were removed) still references `m_sss_apply_active`; left in place,
to be removed in a cleanup pass.
