# Stage 07 — Slice 50: SSS Apply = a tier rebuild (no blank, no downgrade, correct panel)

Supersedes the slice-49 "live re-raster from cached pings" approach, which was a
band-aid: it re-rasterized from the cached *first-paint (Low)* pings, so applying a
setting visibly **downgraded** the High mosaic (read as "data disappearing"), and the
progress UI reused the **import** dialog ("Importing 1 line(s)").

## Systemic model
A gain/imaging Apply is now treated as exactly what it is — a reason to rebuild the
layer's map tier — and routed through the SAME mechanism every map-quality change
already uses:

  setLayerSssDisplay (params, marks dirty)
    → SidescanViewController::applyLiveCorrections(all)
      → prebuildTier(layer, currentQuality)        // builds in the BACKGROUND
        → prebuildTierComplete → applyCachedTier   // ATOMIC swap when ready

Nothing clears the map first. The existing mosaic stays on screen at full quality for
the entire rebuild and is swapped atomically when the corrected tier is ready — so the
data never disappears and never downgrades. This is identical to how a Low→High tier
upgrade already behaves; corrections just feed the same path.

Correctness/agreement pieces remain from earlier slices:
- One shared imaging implementation for waterfall + map (`ui/shared/processing/SssImagingAlgorithms`).
- Raster cache fingerprint includes the gain/imaging params (slice 46) → correct
  invalidation + instant reopen when unchanged.
- Imaging params persisted (slice 46).

## Execution window
Re-added, but in **Processing** mode: the job uses format tag `"COR"` so the dialog
shows "Processing N line(s)" / "Processing" stage — not "Importing". Driven by the
prebuild's `loadingProgress`; closed on `prebuildTierFinished` (emitted on every
outcome, including the early-return guards, so it can't get stuck open). Scoped by
`m_sss_apply_active` so it only appears for an explicit Apply.

## Reverted band-aids
- `SidescanMapLoadTask.Build.cpp`: back to caching the corrected (displayed) pings.
- `repaletteAllLayers` ping-fallback: back to rasterizing the cached pings as-is.
- Removed the downgrading cache-re-raster body from `applyLiveCorrections`.

## Files
- `src/ui/features/map/sidescan/SidescanMapDiagnostics.cpp` (applyLiveCorrections = prebuildTier)
- `src/ui/features/map/sidescan/SidescanMapQuality.cpp` (prebuild emits prebuildTierFinished on guards)
- `src/ui/features/map/sidescan/SidescanMapLoadTask.Build.cpp` (cache corrected pings)
- `src/ui/mainwindow/MainWindow.{h,cpp}` + `coordinators/MainWindow.WaterfallCoordinator.Processing.cpp`

## Verification
- Build green.
- NEEDS VISUAL CHECK: Apply a tool → mosaic stays fully visible, "Processing" panel
  appears with progress, then the corrected mosaic swaps in; no black, no thinning,
  not labelled "Importing".

## Known tradeoff (documented, not patched)
A new param-set rebuilds the tier (decode + raster) once — the cost of full-resolution
corrections on a large survey. Same params / reopen are instant (raster cache). Making
*live iteration on new params* decode-free would require an in-memory ping cache at the
displayed tier (bounded memory) — a deliberate future feature, not bolted on here.
