# Stage 08 — Slice 99 Closure: Project-switch staleness sweep

## Goal
Follow-up QC after the 3D scene leak (slice 98): audit EVERYTHING holding
per-project state for the same defect class — "reset on close, but not on
project→project switch". Systematic pass over bindProjectUi, the
projectAboutToChange handler, WindowRegistry ProjectReplaced consumers, and
every container member in MainWindow.h.

## Audit results

**Clean (verified, no change):**
- 2D map: `MapView::setProject` resets layer data/nav/bbox unconditionally.
- Waterfall + SBP viewer windows: ProjectReplaced → `clearLayer()` (thorough);
  contacts re-pushed from the new project on next `setLayer`.
- Line list, inspector, right panel, Views panel, geodesy, node graph, data
  library, event bus, op coordinator: all re-pointed in bindProjectUi.
- Undo stack cleared twice (PSC + bindProjectUi); ops cancelled; import
  overlay + tools-apply tracking cleared in projectAboutToChange.
- Import/op bookkeeping maps (`m_import_job_ids`, `m_op_job_ids`): entries
  retired by completion/cancel paths.
- In-flight imports hold `shared_ptr<Project>` — memory-safe across a switch.

**Bugs found and fixed (all in this slice):**
1. **Activity log never cleared** — project A's History entries showed under
   project B forever. Fix: `m_activity_log.clear()` + `rebuildHistoryList()`
   in bindProjectUi (before the "Opened project" seed).
2. **Problems panel never cleared on switch** — problems are keyed by layer id
   and only cleared when that layer's diagnostics re-run; the old project's
   layers never re-run, so its problems persisted forever. Fix:
   `m_diag_hub->clearProblems()` in bindProjectUi.
3. **Metadata inspector windows (SSS + SBP) dangled** — lazy top-level windows
   that receive `setProject(currentProject(), …)` only when opened; nothing
   reset them on switch, leaving raw `Project*`/`DataLayer*` pointers into the
   destroyed project (crash risk on any interaction). Fix: close both in
   bindProjectUi (QPointer + WA_DeleteOnClose; no closeEvent overrides that
   could touch the dead project).
4. **Project switch abandoned in-flight imports silently** — the slice-92
   guard only covered app close. Switching (open/new/close project) mid-import
   stranded the imported layers in the abandoned project object (data loss on
   the new save). Fix: `ProjectSessionController::setImportsBusyCheck()` hook
   (wired to `ExecutionController::importsBusy()`), and
   `confirmAbandonImports()` prompt (Continue/Cancel, default Cancel) at the
   top of loadProjectPath / newProject / closeProject.

**Latent (recorded, not fixed):** `LayerPickerWidget` is dead code — never
instantiated (`m_layer_picker` is always null; all call sites are guarded).
If it is ever revived it needs `setProject` wiring into bindProjectUi.

## Verification (temp diag, removed after)
Scripted SBP→SSS switch:
- activity log after switch = exactly one entry ("Opened project …SSS_Only");
- problems = 0; 3D regression check: curtains 0, drapes 4 (slice 98 holds).
Full suite 16/16 green.
