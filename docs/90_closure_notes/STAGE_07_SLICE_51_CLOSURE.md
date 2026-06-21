# Stage 07 — Slice 51: Reliable raster-first reopen (no ping recompute)

## Symptom
After applying corrections, reopening the project was slow again ("is it not baked
in / are we recalculating pings?"). The corrected raster WAS persisted, but reopen
still re-decoded pings.

## Two root causes (both fixed systemically)

### 1. Reopen always staged through a Low ping-decode
`activateLayer` unconditionally painted a fast Low preview for Medium/High tiers and
only then upgraded — so even when the full corrected tier was cached on disk, it
re-decoded pings for the Low stage. **Fix:** before staging, probe the requested
tier's on-disk raster with the new cheap `rastercache::isFresh(path, meta)` (reads
header+meta only). If fresh, build at the requested quality directly → the build's
raster fast-path loads it with **no ping decode, no staging**. Only an *uncached*
slow tier still stages.

### 2. Fingerprint was bit-exact on floats → spurious misses on reopen
The raster-cache fingerprint hashed raw float bytes. Params persist via JSON
(float→double→text→double→float); any precision drift across that round-trip made the
reopened fingerprint differ from the in-session raster's → cache miss → full re-decode
**every** reopen. **Fix:** tolerance-based fingerprint — `makeMeta` now quantizes all
float params (`fnvMixF`, 1e-4 resolution, well below any UI step) before hashing, so a
save/reload round-trip keeps the same fingerprint while real changes still differ.

## Result
- Reopen with a cached corrected raster: instant, zero ping decode.
- Re-applying the same settings: instant (cache hit).
- A genuine param change: rebuilds once, persists, then instant thereafter.

## Tests (tests/test_raster_cache.cpp)
- `isFresh`: matching → true; stale nav_hash / quality / missing file → false.
- Fingerprint: deterministic; **unchanged** under sub-quantum float drift (the
  round-trip case); **changed** under real param change / stage toggle.
- Full suite: 17/17 green.

## Files
- `src/ui/features/map/sidescan/SidescanRasterCache.{h,cpp}` (isFresh + fnvMixF quantization)
- `src/ui/features/map/sidescan/SidescanMapLoadTask.cpp` (direct-load when full tier cached)
- `tests/test_raster_cache.cpp`

## Note
Changing the hash function invalidates pre-existing `.draster` caches once (one
rebuild per layer/tier); subsequent opens are instant.
