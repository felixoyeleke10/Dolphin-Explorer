# Stage 07 Slice 17 — QC follow-ups: processing sidecar + formal marker + queued panel

Clears the deferred items from the Slice 16 QC: processing in-place overwrite (#2),
filename-based sidecar detection (#4), and the visible queued job state (point 5).

## #2 — explicit processing no longer overwrites the original .dlpd
`ProcessingService` and `ProcessingWorkerAdapter` previously wrote to the original
store unless the layer index was a strict subset; the comment even confirmed an
in-place overwrite. Both now **always write a per-layer sidecar** (never the
original — D-04), matching the correction services. Removed the now-redundant
`buildIndex()` subset scan from both.

## #4 — formal sidecar marker (self-describing, not a filename heuristic)
Added `artifact_role` (`kArtifactRoleOriginal` / `kArtifactRoleSidecar`) to
`io::FormatMeta`, persisted in the `.dlpd` **file header** by reusing one byte of the
header's `reserved` field — **no size change, no version bump**, so old caches read
as role 0 (original) = the safe default, and `open()` exposes it via `metadata()`.

All four write sites (2 correction services + 2 processing paths) now decide
overwrite-in-place vs new-sidecar by the **role marker** (preferred), falling back to
the legacy `_<layerId>` filename only for sidecars written before the marker existed,
and stamp `kArtifactRoleSidecar` on every sidecar they write. So a store is treated
as a sidecar because it *says* it is, not because of its name.

## Point 5 — visible Queued state in the bottom panel
Built on the `operationQueued` signal from Slice 16:
- `DiagnosticsHub`: added `JobStatus::Queued`, a `startJob(id)` transition
  (Queued→Running, refreshes the start time), and a `beginJob(..., initial)` param.
  `activeJobCount` now counts Queued + Running.
- `MainWindow`: `operationQueued` → `beginJob(Queued)`; `operationStarted` → flips the
  existing job to Running via `startJob` (or creates it running if it never queued).
- Bottom panel: renders a muted `[QUEUED]` row; the Jobs badge counts queued work.

So map builds parked behind the "map" lane cap (Slice 16) now show honestly as
`[QUEUED]` and flip to `[RUNNING]` when they actually start — no more queued-as-running.

## Loader-through-upgrade (the 4th deferred item) — resolved by design
The status-bar loader reflects first-paint builds (`m_active_builds`), so it clears
once preview data is on the map — which IS "data loaded into map." Staged High/Full
upgrades are refinement and now show honestly as `[QUEUED]`/`[RUNNING]` map-lane jobs
in the panel, so there's no false "all done" while work remains visible. No loader
change made (holding the spinner through background refinement would be worse UX).

## Build / tests
io + app (+ all dependents) compile; exe relinked; all 13 ctest pass — incl.
**ParsedCache, ProjectStorage, XtfReader, SegyReader** confirming the file-header
format change is backward-compatible.
