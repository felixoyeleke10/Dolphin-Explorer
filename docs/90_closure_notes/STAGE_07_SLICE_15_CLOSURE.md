# Stage 07 Slice 15 — Staged map loading + trimmed quality caps (perf)

Addresses "loading into map is slower than parsing the data." Two changes, both
scoped to the sidescan map build path; the waterfall and SBP paths are untouched.

## 1. Trimmed the oversized quality caps ✅
`paramsForQuality` (SidescanMapLoadParams.h). The map mosaic is an *overview*, not
a full-res waterfall, so the caps were far larger than the display can show:

| tier | was (pings / samp / img) | now |
|------|--------------------------|-----|
| High | 25000 / 2048 / 2048 | **16000 / 1024 / 2048** |
| Full fallback (`kFullSafeLimit`) | 40000 | **24000** |

Low/Medium/CoverageOnly unchanged. High (the common "good" tier) now reads ~half
the samples and ~⅔ the pings → roughly 3× lighter build, same output resolution.
Very large files fall back from Full→High sooner.

## 2. Progressive (staged) load for the heavy tiers ✅
The nav track already paints instantly from the index. The mosaic itself was a
single heavy build at the target tier, so High/Full felt slow. Now:

- `activateLayer` computes a `build_quality`: for **High/Full** targets it paints a
  fast **Medium** preview first (`stage_upgrade = true`); CoverageOnly/Low/Medium
  build directly (no change to the common default path).
- After the first paint launches, it kicks off `prebuildTier(layer, m_quality)` —
  the previously-defined-but-**unwired** background tier builder — to build the full
  requested tier off the UI thread (heavy, D-14 capped).
- When the upgrade lands, `prebuildTierComplete` swaps it in, guarded on
  *still-current quality* + *layer still loaded* (a quality switch or unload while
  the upgrade is in flight is ignored — no stale swap).

So a High/Full load now shows a usable Medium mosaic quickly, then sharpens to the
full tier in the background.

### Correctness details
- **No nav jump on swap:** `prebuildTier` previously skipped the display-time nav
  correction that `activateLayer` applies. Added `applySidescanNavCorrections` to
  the prebuild background fn (same model-owned `layer->nav_state`, no-op when none),
  so the upgraded tier matches the first paint.
- **Shared swap helper:** factored the tier→map application out of
  `setMapSonarQuality` into `applyCachedTier(layer_id, quality)`, reused by both the
  instant quality-switch path and the staged-upgrade swap (no duplication).
- First paint caches under `build_quality` (Medium), the upgrade under the target —
  both tiers end up cached, so subsequent quality switches between them are instant.
- `prebuildTier` now stamps `nav_stats.quality_used = quality` so post-swap
  diagnostics report the real tier.

## Build / tests
dolphin-ui-map + dolphin-ui-mainwindow compile; exe relinked. All 13 ctest pass
(incl. SidescanGeoref, NavCorrections, ProjectStorage, PerfBaseline). Needs runtime
verification: load a project at High/Full and confirm the Medium preview appears
fast then sharpens, with no nav shift on the upgrade.

## Close-out QC fix — Bake now honours the D-14 concurrency cap ✅
Reviewing the Slice-14 Bake command revealed a concurrency bug it introduced:
`CorrectionBatchOperator::applySSS/applySBP` (single-layer) call
`service->applyToLine`, which runs on the **global QThreadPool via
`QtConcurrent::run` — uncapped**. `onBakeCorrections` looped over every customized
layer calling those, so baking N layers fired N concurrent heavy read+correct+write
jobs, violating D-14 (cap 2). Only the *batch* path throttles, via its
`kMaxConcurrent` dispatch queue.

The previous batch methods `applyAllSSS/applyAllSBP` applied **uniform** params to
all layers, which doesn't fit per-layer baking — and after the Apply/Bake split they
had no callers (dead). Replaced both with **`bakeCustomized(project)`**: it
enumerates customized SSS/SBP layers and bakes **each layer's own display state**
through the same capped queue + batch + settlement machinery. `onBakeCorrections`
now calls it instead of looping. So Bake throttles to 2 in-flight jobs and shows a
proper batch in the bottom panel. The single-layer SRC-toggle `applySSS` (bottom-pick
persistence) is unchanged — one job, within the cap. Dead code removed; built green.

## Status
Stage 07 destructive-correctness + perf program complete:
- Slice 14 — correction-store safety (no original overwrite) + honest removal warning
- Slice 14 — SeaView Apply/Bake split (SSS + SBP): Apply = live display, Bake = explicit
- **Slice 15 — staged map loading + trimmed caps (this note)**
