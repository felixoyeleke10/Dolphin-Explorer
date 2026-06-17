# Stage 07 Slice 16 — Map-load scheduling + task-system honesty + correctness QC

Responds to a user QC report on (a) map loads fanning out all lines at once,
(b) the bottom panel showing queued work as `[RUNNING]`, plus several correctness
findings.

## Correctness fixes
- **SSS Apply no longer hides an amplitude bake (High).** On an SRC (slant-range)
  toggle with bottom picks, `WaterfallCoordinator` called
  `applySSS(layer, …, toCorrectionParams(p), picks)` — which would bake TVG/ARC/AGC,
  not just persist the picks. Now passes **empty** correction params, so only the
  georef picks are written; amplitude corrections stay live (Bake commits them).
- **Default map quality is now CoverageOnly (Medium).** The QSettings fallback in
  `MainWindow.cpp` and `MainWindow.Menus.cpp` said CoverageOnly in comments but used
  `MapSonarQuality::Low` — a new/missing setting paid raster cost. Both now default
  to `CoverageOnly` (instant coverage + nav, no raster until the user opts in).
- **Stray build logs gitignored (Low).** `/build_out.txt`, `/build_output.txt`.

## Task-system honesty + map-build scheduling (the core)
**`OperationManager` now schedules by named lane instead of one global heavy cap.**
- A `Lane{cap, running, queue}` per name; `run(...)` takes an optional `lane`.
  Resolution: explicit lane → else `"heavy"` when `heavy==true` → else uncapped
  immediate. Default lane cap = 2 (D-14 preserved for `"heavy"`).
- **Honest queued vs running:** `operationStarted` is now emitted only when an op
  *actually launches* (immediately or when pumped off the queue). Ops parked behind
  a lane cap emit the new `operationQueued` signal instead. Previously
  `operationStarted` fired before the queue decision, so capped jobs showed as
  `[RUNNING]` while merely waiting. Existing DiagnosticsHub wiring (operationStarted
  → beginJob) is now truthful with no change — a queued job simply doesn't appear as
  a running job until it runs. `cancelAll`/supersession drain every lane's queue and
  still fire `on_finally` (busy-counter balance preserved).
- **Map builds run in a dedicated `"map"` lane (cap 2), separate from import.**
  `activateLayer` first-paints and `prebuildTier` staged upgrades both use it, so
  loading 12 lines no longer submits 12 builds at once, and High/Full upgrades don't
  fan out across every line — all map work shares 2 slots, independent of the
  import/decode (`"heavy"`) lane.
- **Active line first.** `setMapSonarQuality` now reorders the reload list so the
  active layer enters the map lane first (order = priority under the cap).

The status-bar loader stays correct: `m_active_builds` counts every submitted
first-paint (queued or running), so it clears only once all first-paint data is on
the map; the lane just throttles real concurrency. Staged upgrades refine after and
show honestly as map-lane jobs.

## Build / tests
dolphin-app (+ all dependents) and dolphin-ui-map/-mainwindow compile; exe relinked;
all 13 ctest pass (incl. CancellationToken, TaskRegistry, PerfBaseline).

## Deferred (sequenced follow-ups, with approach)
- **Panel "N queued" display (point 5):** wire `operationQueued` → a
  `DiagnosticsHub::JobStatus::Queued` job + `startJob(id)` transition on
  `operationStarted`; render queued rows distinctly / as a summary. Signal is already
  emitted; only the Hub state + panel render remain.
- **ProcessingService / ProcessingWorkerAdapter in-place overwrite (#2):** explicit
  processing still overwrites a full-store `.dlpd` unless the index is a strict
  subset. Apply the same always-sidecar rule the correction services use, if we want
  "original parsed `.dlpd` always preserved" for processing too.
- **Formal sidecar marker (#4):** sidecar detection is filename-suffix based
  (`_<layerId>`); a metadata flag in the artifact would be a stronger safety marker.
- **Loader-holds-through-upgrade:** if "loaded into map" should mean full quality,
  count `prebuildTier` in the busy state too (currently refinement is post-loader).
