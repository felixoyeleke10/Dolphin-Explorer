# Stage 07 Slice 18 — Background-task panel hang fix (UI-thread churn)

Addresses "the background task window still hangs a lot." Root cause was UI-thread
monopolisation by the bottom panel's Jobs tab, which froze the whole UI (including
the import progress dialog) during job bursts.

## Root cause
- `DiagnosticsHub::m_jobs` was **never pruned** (only the output log was capped).
  Every operation — imports, corrections, and now every map first-paint + staged
  upgrade — appends a job that lives forever, so the list grows unboundedly over a
  session.
- `BottomDockPanel::rebuildJobsTab()` does a **full `clear()` + re-add of every job
  and batch** on **every** `jobChanged`/`batchChanged`. So a burst (12 lines each
  flipping queued→running→done, plus upgrades) triggers many full rebuilds of an
  ever-growing list → O(N²) on the UI thread → visible hangs that worsen over time.
  (The Slice 16/17 honest queued state added more `jobChanged` events, making it
  more noticeable.)
- The import progress dialog itself is *not* the culprit — it updates rows
  incrementally (`findRow` + in-place); it only appeared to hang because the UI
  thread was busy rebuilding the panel.

## Fix
- **Cap job history (`DiagnosticsHub`).** `pruneJobs()` (called from `beginJob`)
  drops the oldest **finished** jobs once over `kMaxJobs = 200`, always keeping
  Running/Queued. Bounds the rebuild cost.
- **Coalesce panel rebuilds (`BottomDockPanel`).** `onJobChanged`/`onBatchChanged`
  now call `scheduleJobsRefresh()`, which collapses a burst into a single rebuild on
  a 40 ms single-shot timer (guard flag) instead of rebuilding per event. The final
  state is always rendered (the last change schedules a refresh; the rebuild reads
  the hub fresh).

Together: the Jobs tab updates at most ~25×/s over a bounded list, so it can't
monopolise the UI thread — the panel and the progress dialog stay responsive.

## Build / tests
dolphin-ui-bottom + dolphin-ui-mainwindow compile; exe relink blocked only by
LNK1168 (app running — close it to pick up the new build). No logic touched outside
the panel/hub display path; no test changes.

## If hangs persist after relinking
Next suspect is map-render cost with many accumulated layers (each repaint draws all
layer images + coverage). That's a rendering-LOD concern, separate from this task-
display fix.
