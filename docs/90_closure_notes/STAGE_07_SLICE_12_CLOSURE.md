# Stage 07 Slice 12 — Finish the nav-state contract + OperationManager hardening

Addresses a QC review: "DataLayer owns nav state means SSS and SBP both store it,
restore it, clear it, serialise it, and test it" — plus OperationManager
completion-order/counter correctness and a stale ParsedCache test.

## 1. (High) nav_state is now persisted  ✅
`DataLayer::nav_state` / `nav_customized` are written/read alongside
`sss_display` / `sbp_display`:
- write: `Project.Serialization.cpp` (only when `nav_customized`, like the display blocks);
- read: `Project.Serialization.Read.cpp` (sets `nav_customized=true` on presence).
- **Test:** `test_project_storage.cpp::testNavStatePersistence` round-trips a
  customized layer (all 7 fields) and asserts an untouched layer reopens
  uncustomized with defaults. ProjectStorage passes.

## 2. (High/Med) SSS nav corrections now match SBP  ✅
- **Apply-to-line stores on the layer.** The nav/heading panels' `applyToLineRequested`
  now route through new `MainWindow::onWaterfallNavProcessLine` (stores
  `nav_state`+`nav_customized`, `markProjectDirty`, then live-applies) — mirrors
  `applySbpNavToLine`. Previously they wired straight to the window (live-only).
- **applyStoredNavParams clears to defaults** when a layer is uncustomized
  (applies `NavProcessingParams{}` instead of early-returning), so switching to a
  line without corrections no longer leaves the previous line's corrections
  visible. Mirrors `applyStoredSbpNavParams`.

## 3. (Med) OperationManager: on_finally now runs AFTER on_done  ✅
The watcher previously ran `on_finally` before `on_done`, so SSS load announced
`loadingFinished()` before the map data was installed. Reordered: cancel/on_done/
fail first, then `on_finally` on every outcome.

## 4. (Med) cancelAll no longer zeroes m_heavy_running  ✅
Already-launched heavy ops keep their watchers and each decrements the counter via
`finishOp` when they finish (even cancelled). Resetting to 0 in `cancelAll` let a
freshly-submitted heavy op exceed the D-14 cap while cancelled jobs drained.
Removed the reset; the counter self-corrects.

## 5. (Med) SSS deactivate no longer resurrected by late finalizers  ✅
`activateLayer`'s `on_finally` now only does Loading→Ready
(`m_active_builds==0 && m_data_state==Loading`). A cancelled build's finalizer
firing after `deactivate()` set Idle no longer flips the viewer back to Ready.

## 7. (Med) ParsedCache footer fast path restores derived metadata  ✅
`buildIndex`'s footer fast path returned without setting `artifact_count`,
`sidescan_ping_count`/`subbottom_trace_count`/`mag_sample_count`/`multibeam_ping_count`,
`start_time`, `end_time` — so a cached reopen left them zero. Now recomputed from
the restored (file-ordered) entries, matching the full-scan result. **ParsedCache
test now passes** (was red).

## 6. (Med/Low) SSS repalette shares the load key — left as-is (rationale)
The bg repalette fallback reuses `"sss:load:"+id` deliberately: it gives
newest-wins supersession, which *prevents* a stale recolour and a reload from both
writing a layer. The fallback only runs for already-loaded layers (cached pings,
no intensity cache) so it does not collide with initial loads; a distinct key
would instead allow a stale repaint to coexist with a reload. Changing it would be
a net regression, so it was not changed.

## Build / tests
All libs compile; **ctest 13/13 pass** (incl. ParsedCache + ProjectStorage's new
nav case; the previously "missing" CancellationToken/TaskRegistry relinked — that
was a transient lib lock). Exe relink blocked only by LNK1168 (app running).

## Post-review follow-ups

### #6 confirmed intentional + documented (no behaviour change)
Traced every path: the repalette fallback's shared `"sss:load:"+id` key is safe
and load-bearing, not a collision risk —
(a) the fallback only runs for already-loaded layers while an initial load runs
for a not-yet-loaded one (no same-layer race);
(b) reloads remove the layer from `m_loaded_layers` first, so a reload correctly
supersedes an in-flight recolour (newest wins);
(c) `unloadLayer`/`deactivate` cancel by that key/prefix, so they also cancel an
in-flight recolour — a distinct key would let its `on_done` write after removal;
(d) the recolour op has no `on_finally`, so supersession never imbalances
`m_active_builds`. Captured as an expanded comment at the call site so QC won't
re-flag it.

### Palette / display-params applied on load (adjacent fix)
`activateLayer`'s success handler shipped the image the background raster baked
with the palette captured at load start and **no display-param overrides** — so a
layer loaded while the palette changed (or while custom gain/contrast was active)
rendered with the wrong look until the next repalette. `on_done` now recolours
from the cached intensity **only when** the current palette differs from the
captured one or `m_display_params` is set to non-default — the common case
(unchanged palette, no custom params) skips the LUT pass, so loads pay nothing
extra. Mirrors the repalette success handler. ui-map compiles; suite green.
