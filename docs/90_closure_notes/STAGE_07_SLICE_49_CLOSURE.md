# Stage 07 — Slice 49: Fast, non-blanking SSS Apply (fix slow + disappearing data + "importing" dialog)

## Symptoms
1. Applying gain/imaging was very slow (84s, whole mosaic re-decoded from disk).
2. The map data disappeared / went black during processing.
3. The progress UI was the **import** dialog — said "Importing 1 line(s) … Reading
   lines", which read as importing, not correcting.

## Fixes
### 1. Removed the import dialog from Apply (slice 48 reverted)
Reusing `ExecutionProgressDialog` made Apply look like an import. Dropped the
`addJob`/`finishJob` wiring and the `m_sss_apply_*` flags; Apply now reports through
the normal status-bar progress. (Kept the harmless `prebuildTierFinished` signal +
prebuild `loadingProgress` so the status bar shows real progress.)

### 2. Disappearing/black data — physical16 masking
The shared chain built its working rows via `SSSAmplitudeProcessor::physical16`, which
masks negative-range (water-column) samples to **0**. The map georeferencer rasterizes
`ping.samples[].amplitude` verbatim, so writing those zeros back blanked large regions
→ black mosaic. Now the chain works on **raw amplitudes** (no masking); the algorithms
already skip zero-valued samples in their statistics.

### 3. Slow + blank — live re-raster from cached pings
- The map build now caches the **pre-correction** normalized pings
  (`result.map_pings_cache`) instead of the post-correction ones.
- New `SidescanViewController::applyLiveCorrections(all_layers)`: re-rasterizes the
  target layer(s) from those cached pings — **no disk decode** — applying the current
  gain/imaging params, and **keeps the existing mosaic on screen until the new one is
  ready** (swap on the worker's on_done → no blank). For staged tiers (Medium/High) it
  then kicks a background `prebuildTier` to refine to full resolution.
- `MainWindow::applySssCorrection` now calls `applyLiveCorrections` instead of
  `reloadLayer`/`reloadCurrentLayer` (which re-decoded and could blank).
- The palette re-colour ping-fallback (`repaletteAllLayers`) now also re-applies the
  layer's chain (cached pings are pre-correction).

## Files
- `src/ui/shared/processing/SssImagingAlgorithms.cpp`
- `src/ui/features/map/sidescan/SidescanMapLoadTask.Build.cpp`
- `src/ui/features/map/sidescan/SidescanMapDiagnostics.cpp`
- `src/ui/features/map/sidescan/SidescanViewController.h`
- `src/ui/mainwindow/MainWindow.{h,cpp}`
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`

## Verification
- Build green.
- NEEDS VISUAL CHECK: Apply a tool → mosaic stays visible, updates quickly with the
  correction (no black, no import dialog); High tier sharpens shortly after. Confirm
  the result is the corrected image, not black.

## Notes
- First Apply after opening (before any map build cached pings) still falls back to a
  full reload for that layer; subsequent Applies are the fast cached path.
- Memory: one pre-correction ping set per loaded layer (same footprint as before — the
  cache previously held post-correction pings).
