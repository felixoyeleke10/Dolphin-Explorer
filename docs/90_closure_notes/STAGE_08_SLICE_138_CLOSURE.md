# Stage 08 Slice 138 Closure — Bounded Map Quality and Raster Cache

## Behavioral goal

Keep every shipped map tier deterministic, cancellable, corruption-safe, and
memory-bounded without silently relabeling High quality or retaining decoded
survey payloads indefinitely.

## What changed

- Every map tier now has explicit ping-group, channel-entry, sample, and image
  bounds. High remains a distinct 4,096-pixel / 1,024-sample product with a
  compile-time combined decoded-sample and georeferenced-point ceiling of
  160 MiB.
- The map retains only the displayed intensity grid. Raw decoded ping caches and
  duplicate completed-tier grids were removed; large cache-hit grids transfer
  ownership during colorization instead of copying.
- Initial loads, tier prebuilds, and streaming decode observe cancellation.
- Raster cache format v2 / algorithm revision 4 fingerprints the complete
  processing and georeference contract. Older algorithms rebuild instead of
  being presented as current output.
- Cache reads reject impossible counts, negative/overflowing dimensions, pixel
  mismatches, truncation, and blank summaries without mutating live map data.
- Cache writes validate dimensions and summary content before publishing.

## File cluster

- `src/ui/features/map/sidescan/SidescanMapLoadParams.h`
- `src/ui/features/map/sidescan/SidescanMapLoadTask*`
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp`
- `src/ui/features/map/sidescan/SidescanRasterCache.*`
- `src/ui/features/map/sidescan/SidescanViewController.h`
- `src/app/services/ImportService.*`

## Verification

- `RasterCache` covers round-trip fidelity, stale metadata, old algorithm
  rejection, corrupt/truncated inputs, invalid writes, full parameter hashing,
  and map auto-stretch colorization.
- `SidescanGeoref` asserts that High is distinct, entry/sample bounded, and below
  the declared working-set ceiling.
- Focused cache/georeference tests passed, the full CTest suite passed 25/25, and
  the complete MSVC/Ninja build linked successfully.

## Remaining risk

- Peak process memory also includes Qt image surfaces, renderer resources, and
  unrelated application state; the declared ceiling intentionally covers the
  dominant decoded-sample plus expanded georeference working set for one High
  map build.

## Gate status

Closed.
