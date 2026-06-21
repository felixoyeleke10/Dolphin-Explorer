# Stage 07 — Slice 52: Fix viewport blank ("app disappears") on open recent project

## Symptom
Clicking a recent project made the whole app appear to vanish for ~half a second,
then reappear.

## Root cause
`ProjectSessionController::loadProjectPath` emits `projectAboutToChange`, whose
MainWindow handler did:
- `m_viewport_host->setUpdatesEnabled(false)`  (blank the GL viewport)
- `m_sss_ctrl->deactivate(true)`               (wipe the map data)

…BEFORE the **synchronous** `Project::open(path)` parse runs on the UI thread.
Updates were only restored later in `firstLayerReady` (a deferred tick after the
parse). So the GL viewport was blanked + emptied for the entire open — the window
read as "disappeared," then "reappeared" when updates came back. The map was already
being cleared a second time, correctly, by `bindProjectUi`'s `ProjectReplaced`
broadcast (which runs *after* the parse).

## Fix
Stop blanking the viewport during the open; keep the existing mosaic on screen and
let the post-parse `ProjectReplaced` clear it immediately before the new project's
layers load:
- `projectAboutToChange`: removed `setUpdatesEnabled(false)`; changed
  `deactivate(true)` → `deactivate(false)` (still cancels in-flight builds + resets
  controller state, but does NOT wipe the map). In-flight ops are also cancelled by
  PSC's `cancelAll()`.
- Removed the now-unneeded `setUpdatesEnabled(true)` re-enables in `projectChanged`
  and `firstLayerReady`.

Transition is now: old mosaic stays visible during the parse → `ProjectReplaced`
clears it → new layers load. No window blank.

## Files
- `src/ui/mainwindow/MainWindow.cpp`

## Verification
- Build green.
- NEEDS VISUAL CHECK: open a recent project → the viewport no longer blanks; the old
  map stays until the new project's layers come in.

## Second cause (same symptom) — Background Tasks dialog auto-pop
`ExecutionProgressDialog::onMapLoadPending` (called per layer on open) scheduled
`showForActiveBatch()` after 400ms and auto-dismissed it via `checkAllDone()` once the
background map builds finished — a top-level window popping up and vanishing on every
open = a second "blink". Removed the auto-show for map-only phases: project-open /
map-build progress now shows only in the status bar + bottom Background Tasks panel.
The dialog still opens for user-initiated batches (import / bake / export) via
`addJob()` and still tracks the map phase once open.
(`src/ui/features/import/ImportProgressDialog.Jobs.cpp`)

## Note / follow-up
`Project::open` is still synchronous on the UI thread, so a very large `.dlp` can
briefly freeze input during the parse (the window keeps its last frame, no longer
blanks). Moving the parse off-thread is a separate enhancement if open latency on big
projects becomes a concern.
