# Stage 07 Slice 14 — Correction-store safety (no original overwrite) + honest removal warning

Addresses the QC "hidden-destructive area": ordinary Apply could overwrite the
original parsed `.dlpd`, and layer removal silently deleted the project cache.

## Done — destructive-data safety (no policy change, pure protection)

### Correction Apply never overwrites the original .dlpd ✅
`SidescanCorrectionService` and `SubBottomCorrectionService` previously defaulted
`write_path = req.store_path` (the original store) and only diverted to a per-layer
sidecar when the layer's index was a **strict subset**. So a single full-store
layer's Apply **replaced the original parsed artifact** (violates D-04).

Now both services **always** write to a per-layer sidecar (`<stem>_<layerId>.dlpd`)
and never touch the original. Idempotent on re-apply: if the layer's store is
already its own sidecar, it overwrites that in place rather than nesting another
`_<layerId>` suffix. The corrected sidecar becomes the layer's store (the proven
subset path already propagated `new_path` to the layer); the original stays as the
durable indexed tier. (Pre-existing re-apply *compounding* — reading already-
corrected data — is unchanged and is solved properly by Slice B below.)

### Layer-removal confirmation is now honest ✅
`onRemoveLayer` / `onRemoveLayers` said only "source file will not be deleted" but
`removeLayer` also deletes the generated `.dlpd` cache when unshared. The dialogs
now state: original source kept; the layer's parsed cache (.dlpd) is also removed
unless another layer still uses it.

## Build / tests
dolphin-app + ui-mainwindow compile; ParsedCache / ProjectStorage / SidescanGeoref
/ NavCorrections pass. Exe relink blocked only by LNK1168 (app running).

## Sequenced plan for the rest (not yet done)

### Slice B — separate Apply (display state) from explicit Bake (.dlpd)  [SSS done ✅]
**Implemented (SSS):** gain/imaging Apply (Line + All) no longer auto-bakes — the
two `m_corr_op->applySSS/applyAllSSS` triggers were removed from the Apply handlers.
Apply is now display-state only: the waterfall renders corrections live
(`applyExternalParams`) and the map shows gain/contrast live
(`paramsApplied → setDisplayParams`). A new explicit, confirmable **Processing →
"Bake Corrections into Data…"** command (`MainWindow::onBakeCorrections`) commits
the full corrections to the `.dlpd` **sidecars** (originals preserved) for the map
mosaic + exports — covering both SSS and SBP layers that have applied corrections.
This is the SeaView/SonarWiz model: live processing + explicit mosaic-generate.
The narrow bottom-pick persistence on SRC toggle is kept (georef data). Built green.

**SBP Apply — done ✅:** all four SBP gain/signal Apply handlers (Line+All) now
store display state + push live to the SBP window
(`m_sbp_win->applyGainParams/applySignalParams`) + `markProjectDirty()`, with **no**
`applySBP/applyAllSBP` bake. The explicit "Bake Corrections into Data…" command
commits SBP (and SSS) corrections to `.dlpd` when the user chooses. Built green.

**Apply/Bake split is now complete for both modalities.** Apply = live display
state; Bake = the explicit Processing command. Remaining program item: Slice C
(staged map loading / perf).

----
(original plan below)

### (orig) Slice B — separate Apply (display state) from explicit Bake (.dlpd)
Goal: ordinary Apply updates project/display state only; baking into `.dlpd` is an
explicit, named, confirmable action. Nav already works this way. The wrinkle for
gain/imaging: the **map** applies gain/contrast/threshold via the per-amplitude LUT
(`colorizeIntensityCache`), but TVG/AGC/ARC are range/window-dependent and today
only reach the map via baking. Options to resolve (product decision):
  1. Map shows LUT-able params live; TVG/AGC/ARC require explicit **Bake** (mosaic-
     style "commit"). Simplest; waterfall still previews everything live.
  2. Teach the SSS rasterizer to apply TVG/AGC/ARC at build time from the layer's
     processing state (display-time, like nav). Most consistent; larger.
Then rename the Apply path off "Baking corrections into…" and add an explicit Bake
command (confirmable).

### Slice C — staged map loading (perf)
`activateLayer` already shows the nav track immediately. Make the mosaic staged:
load a fast low-quality preview first, then upgrade to the target tier in the
background; High/Full become explicit upgrades. Trim the large quality caps
(High = 25k pings / 2048 samp / 2048 px; Full → 4096 px). Optional: avoid decoding
full-resolution samples when the map needs few (`loadAllSidescanPingsFromStore`
truncates *after* decode).
