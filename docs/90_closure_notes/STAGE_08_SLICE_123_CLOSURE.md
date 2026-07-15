# Stage 08 Slice 123 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-123 — QC fixes for the 104–122 batch
- primary goal: close the two findings from the post-batch QC review: the
  import-busy hard-refusal had no operator escape hatch, and NodeGraph v2
  manifests shipped without a schema-version bump

## What Changed

### Import-busy dialogs gain a real escape hatch
Slices 115's refusal dialogs ("wait for imports to finish") were honest but
absolute: a stalled or long import blocked project switch/new/close/delete AND
application exit, leaving Task Manager as the only way out.

- `ProjectSessionController::ensureImportsIdle` now offers **Cancel Imports**
  alongside **Wait** (default). Cancel drops the queued files via the new
  public `ImportJobManager::cancelQueue` (promoted from private; contract
  unchanged — in-flight decodes have no cancellation token and settle
  naturally), then waits up to 20 s in a modal "Stopping imports…" progress
  dialog, pumping the event loop so queued job-completion signals land. The
  transition proceeds only once imports are actually idle; a still-settling
  decode reports "try again in a moment" and stays put — never a promise of
  abandonment the teardown cannot keep.
- `MainWindow::closeEvent` now routes through the same public
  `ensureImportsIdle` ("Exit Dolphin Explorer") instead of its own wait-only
  dialog — one policy on both surfaces.

### Manifest schema v12
Slices 120/122 changed what manifests contain (NodeGraph v2 documents with
edge `to_port` + groups; canonical `artifact_index.source_id`) without bumping
`Project::kSchemaVersion`, so an older build would have opened a new manifest
and silently read its graphs degraded (dropped ports/groups) — exactly what
the forward-compat guard exists to prevent. `kSchemaVersion` is now 12 with
the version history documented. v11 and older manifests open unchanged; the
guard tests derive from the constant and still pass.

## Files Touched

- `src/ui/mainwindow/ProjectSessionController.{h,cpp}`
- `src/ui/mainwindow/MainWindow.Events.cpp`
- `src/ui/mainwindow/MainWindow.Controllers.cpp`
- `src/app/import/ImportJobManager.h`
- `src/app/project/Project.h`

## Tests Or Validation

- Full rebuild clean; ctest 22/22 (PerfBaseline separately green in the full
  23-target run preceding this slice).
- Schema-guard tests (`testSchemaVersionGuard`) derive future/legacy versions
  from `kSchemaVersion`, so they exercise the v12 gate unchanged.
