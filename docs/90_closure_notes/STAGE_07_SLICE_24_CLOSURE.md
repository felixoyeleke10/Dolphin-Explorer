# Stage 07 Slice 24 — Open shows the whole survey as RASTER, not just the active line

## Problem (user QC, confirmed by log)
After Slice 22 (raster cache) + Slice 23 (survey framing), open still showed only ONE
sonar raster:
```
[map] combined: 4 layer(s) visible, 3 with track, 3243 nav pts; ...
[raster] layer_..._1: built from pings + cached (907x1024)   ← only the active line
```
3 of the 4 lines were cheap nav tracks, not rasters. Root cause: the lazy-open policy
(Slice 20) loaded only the active line's mosaic and drew nav tracks for the rest — a
policy that predates the raster cache. With `.draster` now persistent, every line can
show its raster cheaply.

## Fix — non-active overview raster load
`SidescanViewController::activateLayer` gained a `bool as_active = true` parameter.
With `as_active=false` it loads a line's raster onto the map as part of the survey
overview WITHOUT taking selection: no active-layer state, no viewport centring, no
status-bar takeover. Everything else is shared — crucially the **cache-first / ping-
fallback** background load (Slice 22) and the intensity/tier cache population.

`MainWindow` now, for every non-active indexed sidescan line (on open
`firstLayerReady`, and as footerless lines finish reindexing in `cacheLayerReady`):
1. `showNavTrackFromIndex(id)` — instant nav-track overview (zero I/O), so the line is
   visible immediately;
2. `activateLayer(id, project, /*as_active=*/false)` — loads the raster. Cache hit →
   paints with no ping decode (instant on reopen). Cache miss → builds + caches in the
   background ("map" lane, cap 2), upgrading the track to a raster.

When the raster lands for a non-active line, its temporary index track is hidden
(`setNavTrackVisible(id,false)`) so it reads as a swath like the active line (not
swath-plus-centreline) — `setLayerMapData` preserves the earlier track flag, so it's
cleared explicitly.

## Behaviour
- **Reopen (all cached):** every line loads its raster from `.draster` → full survey
  mosaic in seconds, no ping decode. (`[raster] … loaded from cache` for each.)
- **First open / never-rasterized lines:** active line first (priority), others show a
  track immediately then fill in as their rasters build (capped, responsive) and cache.
- The 12-min disaster does NOT return: builds are background, capped at 2, progressive,
  and one-time (cached thereafter) — not eager/synchronous/uncapped like the old loop.

## Build / tests
Full build green, exe relinked. Runtime check (user): open → active raster + others fill
in; close + reopen → all lines load from cache (`[raster] … loaded from cache`), full
mosaic fast. Diagnostics (`[raster]`/`[nav]`/`[map]`/`[timing]`) still on for this round.
