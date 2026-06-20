# Stage 07 Slice 32 — DisplayStateManager (slice 5: nav-correction overlay)

## Goal
Route the per-layer **navigation-correction state** (`DataLayer::nav_state` +
`nav_customized`) through `DisplayStateManager` so it is the single mutate point +
notifier (NavOverlay aspect), matching map quality / global palette / SBP gain.
Previously four handlers across two coordinators wrote `layer->nav_state` directly +
`markProjectDirty()` inline.

## What changed
- **`DisplayStateManager`** gained per-layer nav setters (coordinator over
  `DataLayer::nav_state`):
  - `setLayerNav(layer_id, NavProcessingParams)` — stores params, flags
    `nav_customized=true`, emits `displayStateChanged(layer_id, NavOverlay)`.
  - `clearLayerNav(layer_id)` — resets to default params + `nav_customized=false`,
    emits `NavOverlay` (used by the SSS apply-to-all undo path to reset layers that were
    not in the new param set).
  - Forward-declares `NavProcessingParams` in the header; full type via `DataLayer.h`.
- **`MainWindow.LayerCoordinator`** (SBP nav):
  - `applySbpNavToLine` → `setLayerNav`; keeps live `applyNavToLine` + `buildSbpProfileMap`.
  - `applySbpNavToAll` → `setLayerNav` per SBP layer; keeps the loaded-layer map rebuild.
- **`MainWindow.WaterfallCoordinator`** (SSS nav):
  - `onWaterfallNavProcessLine` → `setLayerNav`; keeps live `applyNavToLine` + SSS map
    `reloadLayer`.
  - `onWaterfallNavProcessAllLines` — the undo command's `apply` lambda now calls
    `setLayerNav` (in the param set) / `clearLayerNav` (not in it), preserving exact
    undo/redo semantics; keeps `applyStoredNavParams` + `reloadCurrentLayer`.

## Dirty tracking
No more inline `markProjectDirty()` in these handlers — the existing MainWindow
`displayStateChanged` bus handler marks the project dirty on any non-empty `layer_id`,
so each per-layer NavOverlay emit covers it (apply-to-all + undo emit per layer).

## Build note
Build hit `C1041 (cannot open program database …mainwindow.pdb)` — orphaned `cl.exe` /
`mspdbsrv.exe` from a prior interrupted parallel build were holding the PDB. Resolved by
killing the stale helpers, deleting the locked PDB, and rebuilding (serially). This is a
build-environment lock, not a code error — `dolphin-ui-systems` + `dolphin-ui-mainwindow`
compile + link clean.

## Runtime verification (manual)
- Apply SSS nav corrections (line + all) → waterfall + SSS map update; reopen persists.
- Apply SBP nav corrections (line + all) → SBP window + map ribbon update; reopen persists.
- Undo/redo of "apply to all sidescan lines" still restores previously-uncustomized lines.

## Remaining DSM migration (next slices)
6. Per-view: 3D settings + waterfall view params owned by DSM (ThreeD/WaterfallView).
7. Finish global-default bridging (background, coord format) so all views read the DSM.
