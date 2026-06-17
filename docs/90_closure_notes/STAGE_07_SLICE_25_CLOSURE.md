# Stage 07 Slice 25 — Background panel doesn't appear on recent-project open

## Why it wasn't coming up
The `ExecutionProgressDialog` ("Background Tasks" panel) is made visible **only** by
`addJob()` (→ `showForActiveBatch()`), which fires for import / reindex / correction
batches. A recent-project open with footers present does **no reindex** → no `addJob` →
the panel never shows. The map-load path (`onMapLoadPending`) only updated the internal
"Building map" stage; it never called `show()`. And the normal open path
(`MainWindow::firstLayerReady`) didn't even call `onMapLoadPending`.

## Fix
- **`firstLayerReady`** now counts every sidescan map load as a pending map-load
  (`onMapLoadPending`): the active line (priority) + each non-active overview line. Each
  is balanced by `loadingFinished → onMapLoadDone`, so the panel's "Building map — X of Y"
  reflects the whole survey.
- **`ExecutionProgressDialog::onMapLoadPending`** now surfaces the panel for map-only
  work — but **deferred ~400 ms**: a fast cached open that finishes under the delay never
  flashes a dialog; the panel only appears when loading actually takes a moment.
- **`checkAllDone`** auto-dismisses the panel when a **map-only** phase completes (no
  import/reindex rows to review). Import/reindex (rows present) keep the manual close so
  per-file results can be reviewed.

## Behaviour
- First open / lines still building (slow): panel appears showing "Building map — X of Y",
  auto-hides when the survey finishes loading.
- Fast cached open (all `.draster` hits, < ~400 ms): no panel — nothing to wait for.
- Import / reindex: unchanged (rows + manual close).

## Build
All libraries compile. Exe relink blocked by `LNK1168` (app was running) — needs a clean
relink (close app → build_quick.bat). No behavioural code depends on the relink beyond
running it.
