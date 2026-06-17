# Stage 07 Slice 06 Closure — System-Based Cleanup #1: Per-Layer Nav State → Model

Part of the "system-based, not patch-patch" QC pass. This slice fixes the
single-source-of-truth violation for navigation corrections and the cross-feature
header it depended on.

## Problem
- Display/gain/signal params lived on the model (`DataLayer::sss_display_state` /
  `sbp_display_state`), but **nav-correction params lived in a `MainWindow`
  runtime map** (`m_layer_nav_params`) — a separate, MainWindow-owned per-layer
  store, inconsistent with every other per-layer setting.
- `NavProcessingParams.h` lived under `ui/features/waterfall/`, so any non-waterfall
  consumer (sub-bottom, the model) created a cross-feature include.

## Fix
1. **Relocated `NavProcessingParams.h`** → `app/display/` (Qt-free, the canonical
   home for these UI-namespace param structs, alongside `SssDisplayState.h`).
   Updated all 9 `src/` includers + 1 test; removed the old header. No app→ui or
   cross-feature include remains.
2. **Added `DataLayer::nav_state` + `nav_customized`** as the single source of
   truth, mirroring the display-state fields.
3. **Migrated every `m_layer_nav_params` use to `layer->nav_state`:**
   - SSS `onWaterfallNavProcessAllLines` now snapshots per-layer `nav_state` for
     undo/redo and the command's apply writes back into the model (and refreshes
     the open waterfall); `applyStoredNavParams` reads `layer->nav_state`.
   - SBP `applySbpNavToLine/All`, `applyStoredSbpNavParams`, and
     `buildSbpProfileMap` read/write `layer->nav_state`.
   - Nav changes now `markProjectDirty()` (consistent with display-state edits).
4. **Deleted the `m_layer_nav_params` member** and its lifecycle glue (the
   `bindProjectUi` clear and the two per-layer `erase` calls on layer removal) —
   state now lives and dies with the layer.

## Result
`MainWindow` holds no per-layer nav state; the model owns it. Build green
(`DolphinExplorer.exe` relinked), 0 errors / 0 warnings.

## Also confirmed clean (no work needed)
- Status-bar progress is only ever driven through `refreshLoadingIndicator()`
  (the earlier loading-indicator fix already removed the direct pokes).
- `m_op_job_ids` / `m_import_job_ids` are the intended OperationManager/import →
  DiagnosticsHub bridges, not patches — they consolidate naturally as part of the
  OperationManager-adoption work below, so they were left in place.

## Remaining QC items (tracked, not in this slice)
- **#1 Background-work unification on `OperationManager`** — the large one:
  `OperationManager::run<T>()` is still called 0 times; ~19 files hand-roll
  `QtConcurrent` + gen-counter + cancel-flag. This is the main "fast & clean" win
  (central concurrency cap per D-14, unified cancel/queue, less duplicated code)
  but it touches the hottest paths and needs runtime verification, so it is its
  own focused slice.
- **#3 Apply-to-all orchestration → service** (medium).
- **#5 Over-broad refresh/reload → targeted, event-driven invalidation** (medium).
