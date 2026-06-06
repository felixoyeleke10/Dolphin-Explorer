# Stage 03 Closure Note

**Stage:** 03 — Performance and Scalability  
**Closed:** 2026-06-06  
**Status:** Complete — baseline numbers pending hardware run

---

## What this stage delivered

### Slice 03A — Benchmark harness

`tests/test_perf_baseline.cpp` and `docs/10_plan/STAGE_03_SLICE_03A_BENCHMARKS.md`.

Four repeatable scenarios covering the main performance-critical paths:
- A. `ParsedCacheReader::buildIndex` at 1k / 10k / 50k synthetic SSS entries
- B. Sequential artifact reads at 10k entries
- C. `Project::fromJson` / `toJson` round-trip at 10L/100C and 50L/500C
- D. XTF `buildIndex` on the reduced real-vendor fixtures

The harness emits grep-friendly `PERF` lines so before/after comparison is a single grep.
The baseline table in the benchmark doc is left for population on the reference workstation.

---

### Slice 03B — Project open responsiveness

`Project::fromJson` previously called `ParsedCacheReader::buildIndex()` inline for every
layer whose artifact index was not embedded in the JSON manifest.  On a W3-class project
(10+ large survey lines) this blocked the UI thread for the full sequential header scan of
every DLPD file.

Fix: `fromJson` now calls `open()` only (reads the 64-byte file header to recover metadata)
and leaves the layer with `index_built = false` plus a valid `artifact_store_path`.  The
artifact index is rebuilt asynchronously by `ImportService::rebuildCacheIndex()`, launched
from the `loadProject` deferred timer immediately after `bindProjectUi`.

`rebuildCacheIndex` runs `ParsedCacheReader::buildIndex()` on a `QtConcurrent` thread and
emits `cacheIndexRebuilt(layer_id)` on completion.  `MainWindow` connects to this signal
to activate the layer in the map/waterfall and auto-select it if nothing is selected yet.

`CancellationToken` is threaded through `rebuildCacheIndex` so rapid project switches cannot
produce stale writes to a defunct project.

**Before:** project-open time proportional to total DLPD file size (blocked UI thread).  
**After:** project-open is O(1) per layer; layers activate progressively as background scans
complete.

---

### Slice 03C — Visible-first loading and map pre-fit

`ArtifactIndex::navExtent()` computes the lat/lon bounding box from index entries already
in RAM (no I/O).  `MapView::fitToExtent()` positions the map viewport from explicit bounds,
respecting the user-interaction flag.

`SidescanViewController::activateLayer` calls both immediately after `setActiveLayer` so
the map centres on the survey area before the first ping byte is read.  The background
load then fills in swath detail — the pre-fit is the instant first step.

**Before:** map viewport blank until full background ping load completed.  
**After:** map centres on survey area immediately on layer selection, zero I/O.

---

## Memory-residency policy

This section documents the current steady-state memory profile for a loaded project.

### Per-layer residency

| Data | When resident | When released |
|------|---------------|---------------|
| `ArtifactIndex` (seek table) | Layer open → project close | `Project::removeLayer` or close |
| SSS ping buffers | Background load only | Discarded after `buildSwathCoverage` |
| `LayerMapData` (nav_track + coverage) | After first activation | `removeLayerData` or project close |
| `IntensityCache` (uint16 per pixel) | After first map activation | Quality change or `reloadLayer` |
| `PrebuiltTier` (full quality level) | After `prebuildTier` call | `setMapSonarQuality(Off)` or close |
| SBP trace buffers | SubBottomWindow load only | Discarded after display build |

### Key rules

1. **Pings are never kept resident.** Load-path reads pings, builds display structures,
   then discards the ping vector. The seek table (`ArtifactIndex`) stays resident because
   it is small and needed for random access.

2. **Intensity cache enables O(pixels) palette changes.** After the first activation, the
   per-pixel uint16 amplitude is cached so palette changes do not require another disk read.
   The cache is cleared on quality-tier changes (pixels are a different resolution at each
   tier) and on `reloadLayer`.

3. **Multi-layer projects load all layers' nav tracks.** Each activated SSS layer contributes
   a `LayerMapData` to `MapView`. For a 50-layer project each `nav_track` is O(pings) QPointF
   vectors — the dominant in-memory structure. No automatic eviction is implemented; all
   activated layers stay resident until project close.

4. **SBP traces are not cached between SubBottomWindow activations.** Switching SBP layers
   always re-reads from the DLPD. This is intentional: SBP files can be large and the
   display pipeline modifies traces in-place.

### Known gaps (future work)

- No LRU eviction for `LayerMapData` or `IntensityCache` — large multi-layer projects
  accumulate all activated layers' data indefinitely.
- No partial-index loading — the full `ArtifactIndex` for every open layer is in RAM
  even if only a small time window is being viewed.
- Nav-track deduplification: port and starboard SSS channels from the same source share
  nav points but are stored twice (one per `DataLayer`).

---

## Hazards from 03A survey — disposition

| Hazard | Status |
|--------|--------|
| 1. `buildIndex` blocks project open (O(file_size) on UI thread) | **Fixed** (03B) |
| 2. `buildIndex` sequential scan, no skip-ahead | Open — structural; acceptable for current file sizes |
| 3. `writeParsedCache` two syscalls per record | Open — micro-optimisation deferred |
| 4. No precomputed bounding box — map viewport blank until load | **Fixed** (03C) |

---

## What was tested

- `test_perf_baseline` — correctness passes for all synthetic scenarios
- `test_parsed_cache` — round-trip and stale-cache detection still passing
- `test_project_storage` — project open/save/reopen with deferred index rebuild
- Cancellation: rapid project-switch during `rebuildCacheIndex` verified by
  `CancellationToken` inspection in code review

---

## What Stage 04 may now assume

- `Project::fromJson` is O(1) per layer and safe to call from any thread context
- `rebuildCacheIndex` is cancellable via `ImportService::cancelPendingRebuild()`
- `ArtifactIndex::navExtent()` is available for instant extent queries
- `MapView::fitToExtent()` is available for pre-positioned viewport display
- The benchmark harness in `tests/test_perf_baseline` can be used for regression checks
