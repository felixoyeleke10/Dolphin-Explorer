# Stage 07 — Slice 45: SSS gain/imaging tools render on the map mosaic

## Goal
The right-panel SSS tools (Gain: TVG/AGC/ARC; Imaging: ARN/Destripe/Beam-Pattern/
ML-Enhance; + SRC) only ever affected the *waterfall* — the map mosaic rasterized
raw per-ping amplitudes and never ran the imaging chain, so clicking Apply in the
main window did nothing visible. Per the product owner: keep every tool listed AND
make the map render it ("add to map pipeline"), with a progress panel on Apply.

## Approach — single source of truth, live re-raster (raster-first)
The post-assembly imaging algorithms lived only in the waterfall feature. Moved their
per-channel cores to a shared module that **both** the waterfall and the map link, and
taught the map build to run the full amplitude pipeline before rasterizing.

- **New `src/ui/shared/processing/SssImagingAlgorithms.{h,cpp}`** (dolphin-ui-shared):
  per-channel `beamPatternChannel / arnChannel / destripeChannel / mlEnhanceChannel`
  (lifted verbatim from the waterfall, refactored to operate on a pointer-list of
  amplitude rows), plus:
  - `applyImagingChain(pings, params)` — splits pings by channel, runs the enabled
    post-assembly algorithms, writes results back to `ping.samples[].amplitude`.
  - `applySssMapCorrections(pings, params)` — full pipeline: TVG/ARC/AGC (app::corrections)
    + imaging chain, honoring already-baked `correction_flags`. Rows are built via
    `SSSAmplitudeProcessor::physical16` for domain parity with the waterfall.
- **`WaterfallProcessingAlgorithms.cpp`** post-functions now delegate to the shared
  cores (waterfall behavior unchanged; one implementation).
- **Map build** (`SidescanMapLoadTask.Build.cpp` + `SidescanMapQuality.cpp`): call
  `imaging::applySssMapCorrections(raw, sss_params)` after nav correction, before
  rasterizing — so the mosaic reflects the tools. Plumbed `WaterfallParams sss_params`
  through `SssLoadInputs` (from `layer->sss_display_state.params`).
- **Raster cache fingerprint** (`SidescanRasterCache` `makeMeta`): now mixes the
  gain/imaging params into the hash, so changing any tool invalidates the cached
  raster and forces a rebuild (raster-first: per-params raster is cached/reused).
- **Apply wiring** (`MainWindow.WaterfallCoordinator.Processing.cpp`
  `applySssCorrection`): store params on the target layer(s) + slant-range, sync the
  live waterfall if open, then `reloadLayer` / `reloadCurrentLayer`. The off-thread
  map build surfaces progress in the background-tasks panel and repaints corrected.

Apply is a live display re-raster — NOT a `.dlpd` write. Committing to `.dlpd` for
export/high-quality remains the explicit Processing → "Bake Corrections" command.

## Files
- NEW `src/ui/shared/processing/SssImagingAlgorithms.{h,cpp}` + CMake
- `src/ui/features/waterfall/processing/WaterfallProcessingAlgorithms.cpp`
- `src/ui/features/map/sidescan/SidescanMapLoadParams.h`
- `src/ui/features/map/sidescan/SidescanMapLoadTask.cpp` / `.Build.cpp`
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp`
- `src/ui/features/map/sidescan/SidescanRasterCache.{h,cpp}`
- `src/ui/mainwindow/MainWindow.h` + `coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`

## Verification
- Full MSVC/Ninja build green.
- NEEDS VISUAL VERIFICATION in the running app: select an SSS layer on the map,
  enable each tool, Apply → progress panel appears, mosaic re-renders with the
  correction; toggle off + Apply restores. Confirm waterfall still matches.

## Notes / follow-ups
- Imaging chain runs on the quality-thinned map ping set (bounded); ML/destripe are
  approximate on thinned data — acceptable for an overview mosaic.
- Heavy tools (ML CLAHE) on High tier + large lines: watch build time; the op is
  off-thread and cached per-params, so only the first Apply pays the cost.
- Architectural win: imaging algorithms are now shared (dolphin-ui-shared) instead of
  waterfall-only — a future `.dlpd` bake of the imaging chain can reuse them too.
