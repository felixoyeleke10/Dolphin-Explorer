# Stage 08 Slice 137 Closure — SSS Geometry and Continuity

## Behavioral goal

Eliminate false transparent stripes caused by navigation repair, ping thinning,
range pairing, and raster-cell geometry while preserving real survey/data gaps.

## What changed

- Navigation repair now learns normal line cadence, interpolates bounded held or
  missing fixes, keeps paired port/starboard poses coherent, and refuses large
  time/distance jumps, unbounded runs, ping resets, and reused ping-number cycles.
- Geographic interpolation and all 2D/3D map interactions use a local unwrapped
  longitude branch across the antimeridian; public coordinates and labels remain
  canonical.
- Decimation omissions remain stitchable, while decoded rejected/unwritable
  records advance an explicit continuity segment that cannot be painted across.
- Coverage, nav tracks, and raster strips share the same heading, nadir-gap,
  slant/ground-range, bottom-pick, and port/starboard policy.
- Adjacent strips pair by physical ground range. Unequal record lengths taper at
  their true edges instead of index-warping or extrapolating samples.
- Raster cells use all four corners, exact triangle coverage, and deterministic
  sub-pixel fallback. Fake dilation and post-raster hole filling were removed.
- Adaptive stitch thresholds account for preview thinning but remain bounded so
  real survey breaks stay transparent.
- Non-finite/oversized segment estimates are clamped before integer conversion.

## File cluster

- `src/geo/GeoUtils.*`
- `src/ui/features/map/MapLongitude.h`
- `src/ui/features/map/MapView*`
- `src/ui/features/map/paint/MapViewPaint.*`
- `src/ui/features/map/sidescan/SssContinuity.h`
- `src/ui/features/map/sidescan/SssGeometryPolicy.h`
- `src/ui/features/map/sidescan/SssGeorefParams.*`
- `src/ui/features/map/sidescan/SidescanSwathGeoreferencer.cpp`
- `src/ui/features/map/sidescan/SssCoverageBuild.cpp`
- `src/ui/features/map/sidescan/SssNavTrackBuild.cpp`
- `src/ui/features/map/sidescan/SssPreviewRasterBuild.cpp`
- `src/ui/features/map/sidescan/SwathRasterizer.h`

## Verification

- `SidescanGeoref` covers held/missing fixes, hard survey breaks, ping reset and
  reuse, antimeridian interpolation/mosaics, shared nadir policy, full trapezoid
  rasterization, sub-pixel cells, honest breaks, thinned cadence, and unequal
  strip sample counts.
- `MapLongitudeBranch` covers canonical/public versus unwrapped/internal map
  coordinate behavior.
- Both focused tests passed, the full CTest suite passed 25/25, and the complete
  project build linked successfully.

## Remaining risk

- Automated tests cover the causal failure modes and alpha-mask continuity.
  Operator visual QA on the original stripe-heavy survey remains the final
  dataset-specific confirmation because that source file is not a repository
  fixture.

## Gate status

Closed for implementation and automated verification.

