# Stage 07 Slice 07 — System-Based Cleanup #2: OperationManager Unification

QC item #1 (route background work through the central `OperationManager` instead
of hand-rolled `QtConcurrent` + generation-counter + cancel-flag sites). Done as
incremental, build-verified steps.

## STATUS: ✅ COMPLETE (compiles + links; runtime-verify pending)
Migrated: SBP profile build; SubBottomWindow (load+proc); WaterfallWindow
(load/repipe/nav); SidescanViewController (per-layer load + repalette). Retired the
modal ProcessingDialog/TaskProgressController. `OperationManager` gained keyed
supersession, D-14 cap + visible queue, `cancelByKey`, `cancelByPrefix`,
token-passing, and `on_finally`. DiagnosticsHub is the single job tracker; the
status-bar spinner remains for viewer-busy. `prebuildTier` is also migrated
(key `sss:prebuild:<id>:<tier>`, heavy) — the SSS controller now has **zero** raw
`QFutureWatcher`/`QtConcurrent` sites.

## QC item #5 (targeted refresh): verified already resolved
Confirmed against current code (not just the 23-day-old debt note): palette is an
O(pixels) `repaletteAllLayers` recolor; quality switching uses the tier cache;
layer-selection is guarded (`currentLayerId()` checks); `LineListPanel` has
targeted `setLayerVisibility`/`updateLayerLabel`/`refreshContacts`. The remaining
`reloadCurrentLayer()` calls are intentional global-geometry rebuilds (CRS apply,
georef apply/reset — they change `m_georef_params` which applies to every layer);
georef *preview*/reset use single-layer `reloadLayer(activeLayerId())`. No
actionable over-broad refresh remains.

## Decision captured
Background-job progress unifies on the **DiagnosticsHub bottom-panel job list**
(via the existing `operation*→beginJob/endJob/failJob/cancelJob` bridge). The
status-bar spinner stays for "a viewer is busy"; the modal `ProcessingDialog`
(`TaskProgressController`) is to be retired as its users migrate.

## Step 1 — OperationManager foundation (no behavior change; nothing called `run` before)
- **Keyed supersession:** `run(..., key, heavy)` cancels any prior op sharing
  `key`. This replaces the per-call-site generation-counter pattern (one key per
  logical task, e.g. `"sbp_profile:<layer-id>"`).
- **Heavy-job concurrency cap + queue (D-14):** `heavy` ops beyond `m_heavy_cap`
  (default 2) queue instead of launching unbounded; `operationStarted` still fires
  so the queue is visible. `setHeavyCap()` pumps the queue.
- **`cancelByKey()`** for keyed cancellation; `cancelAll()` now also drains
  never-launched queued ops (they have no watcher to clean them up).

## Step 2 — First migration: SBP profile-map build
`MainWindow::buildSbpProfileMap` now uses `m_op_mgr->run<LayerMapData>(…,
key="sbp_profile:"+id, heavy=true)`:
- Deleted the hand-rolled `QFutureWatcher` + the entire `m_pending_sbp_builds`
  set (supersession handles duplicate-build prevention).
- Dropped `taskBegin/taskDone` here — DiagnosticsHub tracking comes free via the
  op→job bridge (the agreed progress model).
- Layer removal → `m_op_mgr->cancelByKey("sbp_profile:"+id)` (cancels the
  in-flight build instead of the old "stop tracking, let it finish").
- `bindProjectUi()` → `m_op_mgr->cancelAll()` abandons the previous project's
  background work.

## Build
Green; `DolphinExplorer.exe` relinked; 0 errors / 0 warnings.

## Runtime check requested before continuing
Select / switch SBP layers and apply SBP nav corrections, watching the bottom
panel: the profile build should appear as a DiagnosticsHub job, a rapid
re-select/apply should supersede (not stack) builds, and removing a layer mid-
build should cancel cleanly.

## Step 3 — Cluster 1: retire the modal ProcessingDialog ✅
Confirmed import and processing both have full DiagnosticsHub coverage
(begin/update/end/fail), so the modal dialog was pure redundancy.
- Removed all `taskBegin/taskDone/taskFail` calls from the import and processing
  flows (DiagnosticsHub + the import overlay still track them).
- Deleted `TaskProgressController.{h,cpp}`, `ProcessingDialog.{h,cpp}`, and the now-
  empty `MainWindow.Processing.cpp`; removed `m_task_ctrl`, its construction, the
  delegate methods, and the three CMake entries.
- DiagnosticsHub (bottom panel) is now the single background-job tracker; the
  status-bar spinner remains for viewer-busy. One whole redundant progress system
  removed.
- Build note: a stale `mspdbsrv.exe` held PDB locks (C1041) mid-build; clearing it
  let the build complete cleanly (exe relinked, 0 errors / 0 warnings).

## Step 4 — Cluster 2 foundation: token-passing
`OperationManager::run`'s `fn` now receives the op's `CancellationToken` so a
migrated viewer pipeline can abort mid-flight (e.g. `processTraces`). The one
existing caller (`buildSbpProfileMap`) was updated to the new signature. Build
green. `OperationManager` is now feature-complete for the viewer migration
(keyed supersession + D-14 cap/queue + `cancelByKey` + token-passing).

## Cluster 2 — viewer loads (in progress, one viewer at a time)

### SubBottomWindow ✅ (migrated; runtime-verify)
- Load → `m_op_mgr->run<LoadResult>(…, key="sbpwin:load", heavy=true)`; the bg fn
  catches internally and returns `ok` so `on_done` always runs and sets
  `Failed` on error (no stuck busy state). Replaces `m_load_gen`/`m_load_cancel`.
- Processing → `m_op_mgr->run<…>(…, key="sbpwin:proc")`; replaces
  `m_proc_gen`/`m_proc_cancel`. `setLayer` cancels the prior `sbpwin:proc` so a
  stale proc can't paint the previous layer while the new one loads.
- `clearLayer` → `cancelByKey` for both keys. Deleted `loadToken`/`procToken` and
  the four gen/cancel members; added `setOperationManager`.
- Coordinator injects `m_op_mgr` and drops the `registerExternal`/token shuffling
  (the op is now owned by `OperationManager`); keeps `dataStateChanged →
  refreshLoadingIndicator`.
- SBP load is now `heavy`, so it respects the D-14 cap and shows as a
  DiagnosticsHub job. Build green.
- **Runtime-verify:** open the SBP window, switch lines, drag gain/signal, apply
  nav, cancel mid-load — watch the bottom-panel job + busy spinner for no stuck
  states and no stale-trace flash.

### WaterfallWindow ✅ (migrated; runtime-verify)
- All four pipeline entry points (loadWindow, onRepipeDebounce, scheduleNavProcessing,
  clearLayer) share one key `"wf:pipeline"`, so load/repipe/nav supersede each other
  (they previously shared `m_load_gen`/`m_load_cancel`). Load is `heavy` (D-14 cap);
  the in-load stale-rerun is a nested `run` with the same key.
- Bg fns catch internally and report via `ok`/`load_failed` so `on_done` always runs
  and sets `Failed` (preserving the two distinct failure messages).
- Removed `m_load_gen`/`m_load_cancel`/`loadToken` + the coordinator's
  `registerExternal("waterfall")`; added `setOperationManager`.
- **Runtime-verify:** open the waterfall, scroll/seek (window loads), drag
  gain/imaging (repipe), apply nav, switch lines — watch for no stuck busy/no flash.

### OperationManager `on_finally` ✅ (foundation for the SSS migration)
`run(...)` now takes an optional `on_finally` callback that fires on the main
thread on EVERY outcome (success / failure / supersession / cancel-while-queued —
handled in the watcher, `pumpQueue`, and `cancelAll`). This lets a caller release
balanced state (e.g. a viewer-busy counter) regardless of how the op ends, which
`on_done` (success-only) can't. Build green; existing callers unaffected (defaulted).

### SidescanViewController ✅ (migrated; runtime-verify)
Multi-layer concurrent loads, now keyed per layer `"sss:load:<layer-id>"`:
- `activateLayer` async path → `run<SidescanLoadResult>(…, key, heavy=true,
  on_finally)`. The bg fn already caught internally (returns `load_failed`); the
  per-layer key replaces the generation guard (distinct keys → other layers still
  accumulate). `on_finally` emits `loadingFinished` + balances `m_active_builds`
  on every outcome (so the busy spinner + import "Loading into map…" counter can't
  stick). Sync early-returns keep their direct `loadingFinished`. `*cancel.flag()`
  bridges the token to `buildSwathPreviewImage(const std::atomic_bool&)`.
- `repaletteAllLayers` fallback → same per-layer key (heavy=false), so recolour
  and load supersede each other for a layer.
- `unloadLayer` → `cancelByKey`; `deactivate` / `setMapSonarQuality` →
  `cancelByPrefix("sss:load:")`. Deleted `m_layer_generations` +
  `m_layer_cancel_flags`. Added `cancelByPrefix` + `setOperationManager`.
- Build: all four SSS files compile clean (exe link blocked only by the running app).
- **Runtime-verify:** open several SSS layers (heavy cap queues them), switch/cancel
  mid-load, change quality + palette, remove a layer mid-build, close project —
  watch the busy spinner + "Loading into map…" never stick, and layers still
  accumulate on the map.
- Note: `prebuildTier` (ProcessingWindow prebuild) still uses a self-contained
  `QFutureWatcher` + local flag (no shared maps); optional future migration.

### (historical) SidescanViewController plan — superseded by the ✅ above
Multi-layer concurrent loads; each load couples to `m_active_builds` +
`loadingStarted/loadingFinished` (which drive the import "Loading into map…"
counter via `onMapLoadDone`). Logic-all-or-nothing: per-layer keys must replace
`m_layer_generations` + `m_layer_cancel_flags` across **all** of
`activateLayer` / `repaletteAllLayers` / `unloadLayer` / `deactivate` at once.
Concrete plan:
1. Inject `OperationManager*` (setter from MainWindow.cpp where `m_sss_ctrl` is built).
2. `activateLayer`: keep the synchronous early-returns' direct `loadingFinished`.
   For the async path → `run<SidescanLoadResult>(name, fn(token), on_done, key =
   "sss:load:"+layer_id, heavy=true, on_finally=[emit loadingFinished; if
   (--m_active_builds==0) m_data_state=Ready])`. Move the result-apply block into
   `on_done`; the per-layer key replaces the generation guard (cross-layer loads
   still accumulate — distinct keys). Drop the `++m_active_builds`/`loadingStarted`
   pre-step into the pre-run section.
3. `repaletteAllLayers` fallback rebuild → same key so it supersedes/accumulates
   consistently with loads.
4. `unloadLayer` / `deactivate` → `cancelByKey("sss:load:"+id)` / `cancelAll`,
   then delete `m_layer_generations` + `m_layer_cancel_flags`.
5. Runtime-verify: open several SSS layers (cap queues them), switch/cancel, change
   quality/palette, close project — watch the busy spinner + "Loading into map…"
   never stick.
1. Inject the `OperationManager*` (setter from the owning coordinator).
2. Replace the `m_*_gen` + `m_*_cancel` + `QFutureWatcher` with
   `m_op_mgr->run<…>(name, fn(token), on_done, key="<viewer>:load|proc", heavy=true)`.
   The key supersedes prior loads (drops the generation guard); `heavy` applies
   the D-14 cap to currently-unbounded concurrent layer loads.
3. **Preserve failure state:** have `fn` catch internally and return an
   `ok=false` result so `on_done` always runs and can set
   `ViewerDataState::Failed` — otherwise a failed load leaves the busy spinner
   stuck (regressing the Slice-04 loading-indicator fix).
4. Keep `setDataState` (drives `anyViewerBusy`); drop the coordinators'
   `registerExternal`/`loadToken`/`procToken` wiring (the op is now owned by
   `OperationManager`, so `cancelAll`/`cancelByKey` cover it).
5. Runtime-verify each: load → switch line → cancel mid-load → induce failure →
   reload, watching the bottom-panel job + the busy spinner.

## Remaining QC items after cluster 2
- #3 apply-to-all → service; #5 targeted refresh.
