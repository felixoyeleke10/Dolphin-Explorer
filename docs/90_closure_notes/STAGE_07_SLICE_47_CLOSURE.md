# Stage 07 — Slice 47: Speed up the corrected-mosaic rebuild

## Context
After slice 45/46, changing a gain/imaging setting correctly invalidates the SSS map
raster cache and rebuilds the corrected mosaic. The product owner confirmed the
slowness is **only the first open after a change** (subsequent opens hit the cache),
i.e. the one-time cost of building the corrected raster for a new param-set.

## Cost breakdown (first build of a new param-set, per tier)
1. Decode all pings for the tier from the .dlpd store — the bulk (High = all pings).
2. Run the gain/imaging chain.
3. Rasterize + persist the raster.

(2) is the only part this feature added; (1)/(3) are pre-existing tier-build costs.

## Speedups applied
- **Concurrent channels**: `imaging::applyImagingChain` now runs port and stbd on
  separate threads (`std::async`); the caller is already a background worker, so this
  ~halves the chain's wall time. (`SssImagingAlgorithms.cpp`)
- **Skip when no raster**: the chain is not run for CoverageOnly builds
  (`max_image_dim == 0`) — the corrected amplitudes would be discarded.
  (`SidescanMapLoadTask.Build.cpp`, `SidescanMapQuality.cpp`)

## Already-good behaviour (no change needed)
- Staged upgrade paints a corrected **Low** preview immediately, then refines the
  requested tier in the background — the map is usable while "Prebuilding" runs.
- Per-param raster is cached, so repeat opens with the same settings are instant.

## Remaining levers (not done — would need product sign-off)
- **Decoded-ping cache**: hold raw decoded pings per layer so *same-session* iterative
  tweaking (adjust → Apply → adjust) re-runs only chain+raster, skipping the decode.
  Cross-session (reopen) still decodes once. Costs memory (raw pings per layer).
- **Map quality tier**: High decodes every ping; Medium/Low are far faster to rebuild.
- **Prebuild-on-apply persistence**: ensure the high-tier prebuild finishes/caches
  during the Apply session so the *next* open is already warm (lost if the user closes
  mid-prebuild).

## Files
- `src/ui/shared/processing/SssImagingAlgorithms.cpp`
- `src/ui/features/map/sidescan/SidescanMapLoadTask.Build.cpp`
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp`

## Verification
- Build green. Visual: change a tool → first open rebuilds (faster, parallel chain) →
  repeat open instant.
