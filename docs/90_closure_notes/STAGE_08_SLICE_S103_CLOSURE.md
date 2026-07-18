# Stage 08 / Slice S-103 — Canonical SSS display processing

> Superseded by Stage 08 / Slice S-136. This note records the earlier local-set
> pipeline step; S-136 replaces its viewer-subset assumptions with a bounded,
> canonical line context and removes the destructive AGC-disabled normalization.

## Behavioral goal

Make the map and waterfall consume one SSS display-amplitude contract instead
of independently ordering normalization, enhancement, and auto-stretch.

## What changed

- Added shared calibration, full display-pipeline, and percentile-stretch APIs to
  `SssImagingAlgorithms`.
- Moved the AGC-disabled robust per-channel normalization into that shared path.
- The map now runs the canonical display pipeline and derives contrast from the
  complete processed ping product, not a sparse sample of georeferenced strips.
- The waterfall now assembles its displayed rows from the same canonical
  processed pings. Seabed detection still receives a clean calibrated copy, so
  display enhancements cannot bias bottom picks.
- Removed the waterfall widget's private processing wrappers and routed its
  synchronous and asynchronous entry points through the canonical orchestration.

## Files touched

- `src/ui/shared/processing/SssImagingAlgorithms.h`
- `src/ui/shared/processing/SssImagingAlgorithms.cpp`
- `src/ui/features/waterfall/WaterfallView.h`
- `src/ui/features/waterfall/WaterfallViewData.cpp`
- `src/ui/features/waterfall/WaterfallViewProcessing.cpp`
- `src/ui/features/map/sidescan/SssPreviewRasterBuild.cpp`
- `tests/test_waterfall_processing_algorithms.cpp`

## Verification

- Built `dolphin-ui-shared`, `dolphin-ui-waterfall`, `dolphin-ui-map`, and
  `test_waterfall_processing_algorithms` with the repository MSVC/Ninja toolchain.
- Added a sample-for-sample regression asserting that the map-facing canonical
  product and waterfall output rows are identical with destriping enabled, and
  that both use identical stretch bounds.
- CTest passed:
  - `ProjectStorage`
  - `SidescanProcessingCoordinator`
  - `RasterCache`
  - `WaterfallProcessingAlgorithms`

## Remaining risk

Map-only geometric interpolation can still make spatial seams visible at some
zoom levels; it no longer changes the underlying amplitude processing or
contrast policy. Any remaining seam is therefore isolated to raster geometry.

## Gate status

Superseded. See `STAGE_08_SLICE_S136_CLOSURE.md` for the current behavior and
verification record.
