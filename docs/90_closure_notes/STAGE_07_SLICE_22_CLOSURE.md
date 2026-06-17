# Stage 07 Slice 22 — Raster-first: persist the built map raster, reopen from it

## Goal (user directive)
> "the problem with the slow reloading (opening recent project) is reparsing the
> pings.. the data should only be parsed once! and then converted into raster! i
> believe the raster is what we work with, thats what professional softwares do."

Until now every map activation re-read pings from the `.dlpd` and re-rasterized the
mosaic on the fly; the rendered raster (`IntensityCache` + coverage + nav track) only
ever lived in memory and was thrown away on close. So reopening a project re-decoded and
re-rasterized every line = minutes. Footer persistence (Slice 21) only sped the *index*.

## What changed
New derived artifact: a **persisted map raster** written next to the `.dlpd`, loaded on
reopen instead of reparsing pings. "Parse once, then work from the raster."

- **`ui/features/map/sidescan/SidescanRasterCache.{h,cpp}`** (new) — binary
  (de)serialization of a built `LayerMapData`: kind, bounds, `is_projected`,
  `show_nav_track`, nav track, coverage ribbons, the **intensity grid** (w/h/disp +
  raw `uint16` pixels = the raster), and a diagnostics-relevant `NavStats` subset. Plus
  a `Summary` (sample nav, track length, ping counts) so the status bar is identical on
  the cache path. Atomic write via `QSaveFile`; `QDataStream` payload.
  - **Path:** `<store_path>.<layerId>.q<quality>.draster` (one file per layer per tier).
  - **Freshness (`Meta`):** store file size + mtime, a hash of the nav-correction params
    + slant-range flag, and the quality tier. Palette is **not** in the key — the image
    is recoloured from the persisted intensity grid on load. Stale ⇒ ignored ⇒ rebuild.

- **`SidescanMapLoadTask.cpp` (`activateLayer`)** — the background task now:
  1. **Fast path:** `rastercache::load(...)` first. On a fresh hit it reconstructs the
     map data + status summary, colourises the preview from the intensity grid, and
     **returns with zero store I/O / no rasterization**.
  2. **Slow path (cache miss/stale):** the existing ping decode + build, then
     `rastercache::save(...)` so the next open is fast.

- **`SidescanMapQuality.cpp` (`prebuildTier`)** — same load-first / save-after for the
  staged High/Full tiers (keyed by the target quality), so high-detail reopens also skip
  ping decode.

- **Logging (temporary):** `[raster] <layer>: loaded from cache (WxH) — no ping decode`
  vs `[raster] <layer>: built from pings + cached (WxH)` → `dolphin_debug.log`. Remove
  once verified.

## Invalidation correctness
- Bake/processing writes a per-layer sidecar ⇒ `artifact_store_path` changes ⇒ different
  cache path ⇒ natural rebuild (Slice 14/17 sidecar model).
- In-place store change ⇒ size/mtime mismatch ⇒ rebuild.
- Nav correction / slant-range change ⇒ nav_hash mismatch ⇒ rebuild.
- **Display-CRS change ⇒ rebuild.** The raster stores coordinates already reprojected
  to the project display CRS, so `makeMeta` folds `displaySpatialRef().id` into the hash.
- Quality tier ⇒ separate file per tier.
- Footer append (Slice 21) changes the `.dlpd` once; at most one extra rebuild, then
  stable (footer is only appended when absent; reads don't touch mtime).

## Loose-end fixes (audit pass)
- **Orphan cleanup.** `Project::removeLayer` now deletes this layer's `.draster`
  sidecars (and all of a store's sidecars when the store itself is removed);
  `Project::purgeOrphanedCaches` deletes `.draster` files whose store is no longer
  referenced. Previously both only handled `.dlpd`/`.dpcache`, leaving raster sidecars
  orphaned.
- **Serialization verified by test.** New `tests/test_raster_cache.cpp` (CTest
  `RasterCache`, links `dolphin-ui-map`) round-trips a populated `LayerMapData` (bounds,
  NaN-gap nav track, coverage ribbons, intensity grid, NavStats, Summary) and asserts
  every staleness field (nav_hash / src_size / src_mtime / quality) and a missing file
  are rejected. **Runs green.**

## Net behaviour
- First open / first import of a line: full ping build (as before) **+ writes the raster**.
- Every open after: `[raster] loaded from cache` → seconds, no reparsing.

## Build / tests
`dolphin-ui-map` compiles; exe relinks clean. Runtime verification pending (user):
open the project once (logs `built from pings + cached`), **close and reopen** (should log
`loaded from cache — no ping decode` and open in seconds).

## Still open (carried from Slice 21)
- "1 of 6 lines": nav tracks build (`[nav]` shows 1016/1128/1097 pts) but only the active
  mosaic shows. Added bounds + a `[map] combined` log in `MapView::rebuildCombined` to
  settle coords-vs-render — needs one run + reading `dolphin_debug.log`.
- Remove the `[raster]` / `[nav]` / `[map]` / `[timing]` diagnostics once perf + render
  are confirmed.
