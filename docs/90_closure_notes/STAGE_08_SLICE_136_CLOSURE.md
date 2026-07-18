# Stage 08 Slice 136 Closure — Canonical SSS Amplitude Context

## Behavioral goal

Make a decoded sidescan record produce the same display amplitude and contrast
in the map and waterfall, independent of map quality thinning or waterfall
window boundaries.

## What changed

- Added a bounded, deterministic line-amplitude context shared by map and
  waterfall. It samples at most 1,024 ping groups / 2,048 channel records with
  256 samples per record and retains at most eight line contexts in process.
- Native per-ping TVG, ARC, and Global AGC now run before resolution-only sample
  compaction. Variable AGC, beam normalization, ARN, destriping, and ML
  enhancement are represented by the same per-channel line gain field in both
  viewers.
- Context lookup uses record identity and timestamp interpolation, with a hard
  five-second survey-gap boundary so processing cannot bleed across line breaks.
- Auto-stretch is computed once from the line context and reused by both
  viewers. The application-wide “Auto stretch” switch now also controls the map;
  explicit display bounds still take precedence.
- Removed the destructive AGC-disabled pre-normalization. Corrected the
  auto-stretch histogram for low-bin and saturated-only data.
- Global and Variable AGC, beam-pattern, and destriping paths now respect baked
  correction flags per record, including mixed/legacy stores.
- Variable AGC now honors its smoothing window/type and the one-record
  along-track window exactly.
- Waterfall draft controls remain drafts until Apply; repipes build/reuse the
  matching context and stale work is superseded through the normal operation
  path.

## File cluster

- `src/ui/shared/processing/SssAmplitudeContext.*`
- `src/ui/shared/processing/SssImagingAlgorithms.*`
- `src/app/corrections/CorrectionAlgorithms.cpp`
- `src/app/services/ImportService.*`
- `src/ui/features/waterfall/WaterfallView*`
- `src/ui/features/waterfall/WaterfallWindow*`
- `src/ui/features/map/sidescan/SidescanMapLoadTask*`
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp`
- `src/ui/features/map/sidescan/SssPreviewRasterBuild.cpp`
- `src/ui/mainwindow/MainWindow.Controllers.cpp`
- `src/ui/mainwindow/MainWindow.Runtime.cpp`

## Verification

- Added sample-for-sample regressions for full-line, waterfall-subwindow, and
  compacted-map products under Variable AGC plus beam/ARN/destripe/ML.
- Added regressions for native calibration-before-compaction, mixed baked flags,
  AGC smoothing, low/saturated stretch intervals, and map auto-stretch semantics.
- Focused CTest set passed 5/5.
- Full CTest suite passed 25/25.
- Complete MSVC/Ninja project build and final `DolphinExplorer.exe` link passed.
- `git diff --check` passed.

## Remaining risk

- The line context is a bounded derived in-process product. Persisted map
  rasters remain restart-fast; opening a waterfall for the first time after a
  restart rebuilds its context once from the durable artifact store.

## Gate status

Closed. This supersedes the local-viewer-set assumptions recorded in S-103.

