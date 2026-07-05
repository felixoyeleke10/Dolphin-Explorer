# Stage 08 — Slice 98 Closure: 3D scene leaked across project switches

## Symptom (user-reported)
Open project A, then open project B (save prompt behaves fine). Panels and the
2D map show project B — but the 3D view still shows project A's data. Most
visible as: SSS project open, switch 2D→3D, the previous SBP survey's curtains
appear ("sss 2d, when i click on switch to 3d, its showing sbp").

## Root cause
`MainWindow::bindProjectUi()` (MainWindow.ProjectBinding.cpp) only called
`m_viewport_host->clearScene()` when the new project was NULL (close), never
on a project→project switch. The 2D view was safe (`MapView::setProject`
resets its layer data unconditionally), but the 3D scene (curtain layers,
drape layers, terrain) is populated additively and had no other reset path —
the old project's geometry accumulated under the new project's.

## Fix
`clearScene()` now runs on EVERY project change. Consequences handled:
- The draping-surface swap block no longer needs the diff-and-remove dance;
  clearScene drops loaded terrain, so it resets `m_loaded_draping` and loads
  the new project's stored surface from a clean slate.
- Placement (in bindProjectUi, after the parse) preserves the deliberate
  "don't blank the GL viewport during the synchronous parse" behaviour
  documented in the projectAboutToChange handler.

## Verification (temp diag, removed after)
Scripted SBP→SSS switch:
- before fix: after switch, `curtains=3` (old SBP) + `drapes=4` (new SSS) —
  leak reproduced;
- after fix: `curtains=0 drapes=4`, and all 4 SSS layers visible on the 2D map.
Full suite 16/16 green.

## Related
The earlier "why is sbp project leaking to show sss" report (slice ~93 era)
was very likely this same defect observed in the other direction.
