# Stage 07 Slice 20 — Worker-adapter restore, ProcessedLayer rename, lazy project open

Three items from a QC pass over `ProcessingWorkerAdapter` + project-open.

## 1. ProcessingWorkerAdapter — restored an accidental deletion
`collectOutputs` was missing the block that computes `meta` and `write_path` (the
lines between the `if (!layer) return;` guard and `writeArtifactBufferToCache`), so
both were undefined where used — it would not have compiled. Restored the
always-sidecar logic (read source meta → role/legacy-name detection → per-layer
sidecar `write_path` → stamp `kArtifactRoleSidecar`). `buildSource` was intact.

## 2. Contract honesty — ProcessedSidescanLayer → ProcessedLayer (+ modality)
`collectOutputs` always tagged its output `ContractType::ProcessedSidescanLayer`,
even for SBP/MAG layers. Renamed the contract type + struct to **`ProcessedLayer`**
(generic) and added a `Modality modality` field set from the layer, so the contract
is honest for any modality. Updated all references: `ContractTypes.h`,
`ContractEnvelope.h`, `ContractStore.cpp` (fingerprint + `contractTypeName`),
`WorkerTypes.h`, and both producers (`ProcessingWorkerAdapter`,
`ProjectOperationCoordinator`, which now set `pl.modality = layer->modality`).
Safe rename: `ContractStore` is in-memory only (no on-disk type-name to break).

## 3. Lazy project open (perf) — the main finding
`firstLayerReady` looped over **every** indexed layer and called
`m_sss_ctrl->activateLayer(...)` for each sidescan line — so opening a project
eagerly built the **entire** mosaic. Even with the capped "map" lane that's far too
much work for an open action.

Fixed: the handler now loads **only the selected/restored layer**, via the
`onLayerSelected(first_layer_id)` call that was already there (it does the full
per-modality activation: SSS swath / SBP profile / MAG track). The eager all-layer
loop was removed. Other lines now load lazily when the user selects them; populating
the whole survey becomes an explicit action rather than part of open.

So project open = read manifest + restore tree + load just the active layer. Fast.

## Build
`dolphin-app` and `dolphin-ui-mainwindow` libraries compile clean (exe relink waits
for the app to close — LNK1168). No test covers these UI/contract paths; needs a
runtime check: open a multi-line project and confirm it opens fast with only the
selected line loaded, and selecting another line loads it on demand.

## Follow-up (optional)
A cheap "all nav tracks" survey overview on open (without the swaths), and/or an
explicit "Load all into map" action, would restore the full-survey picture lazily.
Sidescan nav tracks currently require `activateLayer`, so that needs a light
index-only nav extraction for SSS — separate change.
