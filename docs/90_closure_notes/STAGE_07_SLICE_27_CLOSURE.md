# Stage 07 Slice 27 — Modality-aware import (Codex QC fixes)

## Problem (Codex QC)
The import path treated a *source-level* valid cache as "already imported," so the
modality-by-modality workflow broke:
1. **High — reuse not modality-aware.** Import a mixed XTF as SSS, later import the same
   file as SBP → `classifyImportAction` saw a valid cache for the source and returned
   `ReuseExisting`, which just re-completes the existing SSS layer. **No SBP layer was
   created.**
2. **High — rebuild not modality-aware.** Same gap on the stale-cache path: a missing
   modality layer was never created.
3. **Medium — SBP-only import still caches all modalities.** The cache builder ignores the
   module filter and decodes every family.

## Fix — a modality-aware import system
The cache deliberately holds **all** families (see "Design note" below), so creating a
missing modality is cheap: read the existing cache, route that family to a new layer, no
re-decode.

- **Classifier is modality-aware.** `classifyImportAction(path, project, requested_modules)`
  now decides per requested modality:
  - a requested modality with **no layer yet** → `ImportNew` (creates it);
  - every requested modality already present + valid cache → `ReuseExisting`;
  - every requested modality present + stale cache → `RebuildExisting`.
  Empty `requested_modules` = legacy whole-file import (unchanged source-level behaviour).
  `ImportReviewWizard` passes the chosen module(s) to the classifier.
- **`ImportNew` already handles "add a modality to an existing source"**: `importFile`
  reuses the existing source, `buildArtifactStore` reuses the fingerprint-matched cache
  (no decode), and `completeImport` module-routes the requested family into a new layer.
- **Module routing is idempotent.** `completeImport` skips any family that already has a
  layer for the source, so re-import / multi-module requests never duplicate a modality.
- **Sibling refresh on cache rebuild.** If the source changed and the cache was re-decoded
  (`ImportTaskResult::cache_rebuilt`), existing raw sibling layers (same store) get their
  indices refreshed from the new index so their offsets stay valid; processed/sidecar
  layers are left alone. Covers the stale-cache + new-modality case.

So `RebuildExisting` no longer needs to create missing modalities (those go through
`ImportNew`) — finding #2 is closed by routing, not by special-casing reindex.

## Design note on finding #3 (kept intentionally)
The cache **does** index/decode all modalities even for a single-modality import. This is
**deliberate and required** for the modality-by-modality workflow: it lets a later
modality import (e.g. SBP after SSS) materialise its layer from the existing cache with
**zero re-decode**. Filtering the decode would force a re-parse on every new modality —
the opposite of "import once, add modalities cheaply." Documented here so it isn't
"fixed" into a regression.

## Test
`tests/test_import_classifier.cpp` gains `testClassifyModalityAware`: a mixed source with
only an SSS layer (valid cache) → requesting SBP returns `ImportNew` (not Reuse);
requesting SSS still reuses; legacy no-filter still reuses. `ImportClassifier` now 24
checks; **full ctest suite 14/14 passed** (no regressions).

## Build
All libraries + tests compile; suite green. Exe relink as usual needs the app closed.
