# Stage 07 — Slice 41: Decompose remaining 600+ monoliths

## Goal
Break down the four actionable 600+ line source files flagged by the modularization
scan, splitting by responsibility boundary (not arbitrary line count) so each unit
fits the file-size policy (600+ = split) and follows the aspect-file convention.

The plan's previous top-3 (SBPMetadataWindow, ImportReviewWizard, AppSettingsDialog)
were already split in earlier work; AppStyleDialogs.cpp (660, QSS) was deliberately
left as a single declarative concern.

## What changed

### 1. PanelChatWidget (736 → 351 + 410)
- `PanelChatWidget.cpp` — chat chrome: construction, input handling, empty state,
  message-bubble rendering.
- `PanelChatWidget.Ollama.cpp` (new) — the local Ollama backend: setup chain
  (check → start server → pull model → chat), streaming parse, process management,
  setup/error status. Backend constants (URLs, retries, system prompt) moved with it.

### 2. MainWindow.WaterfallCoordinator (723 → 352 + 247 + 172)
Carried three concerns; split into three:
- `MainWindow.WaterfallCoordinator.cpp` — waterfall window lifecycle: open/wiring,
  prev/next line, cursor, params-applied, metadata, settings.
- `MainWindow.WaterfallCoordinator.Processing.cpp` (new) — layer processing applied
  from the waterfall: source-CRS change, nav corrections (single + all lines),
  bake-corrections, global palette propagation.
- `MainWindow.ContactCoordinator.cpp` (new) — contact creation/selection and the
  Contact Manager window's lifecycle + undoable-edit wiring (a distinct feature).

### 3. SidescanMapLoadTask (645 → 435 + 259)
- `SidescanMapLoadTask.cpp` — main-thread orchestration: guards, snapshot gather,
  the inputs-struct build, the OperationManager run() wiring, and the on_done apply.
- `SidescanMapLoadTask.Build.cpp` (new) — `buildSidescanLoadResult()`: the entire
  off-thread raster build (raster fast-path, bounded preview index, ping load + nav
  correction + reprojection, coverage/track/raster, status pre-compute, persistence),
  extracted from the inline lambda into a free function that is **pure w.r.t. the
  controller** — all inputs arrive via a new `SssLoadInputs` snapshot struct, progress
  via a `std::function<void(int)>` callback, cancellation via the token. (The header
  already anticipated this "load-finish split.")

### 4. ImportProgressDialog (612 → 247 + 397)
- `ImportProgressDialog.cpp` — construction, layout, card/chip building, window chrome
  (tick, run-in-background, show/close).
- `ImportProgressDialog.Jobs.cpp` (new) — job/state model + progress logic: add/update/
  finish/fail, row bookkeeping, header/stage/overall-progress, all-done detection,
  map-load phase counters.

`src/ui/CMakeLists.txt` updated to list all six new sources.

## Compliance
- No behavioural change. The only non-mechanical move (SSS background build → free
  function) preserves every line of logic; the lambda's `this`/`as_active` captures
  collapse into the caller-built `report` callback (signal emission stays in the
  controller TU) and the only `this`-dependent call (`colorizeIntensityCache`) is a
  static member. No band-aid — this is the systemic separation the header anticipated.
- All resulting files within policy: largest is 435 (review band), none in the split band.
- Layer rules unchanged; aspect-file naming matches MainWindow.* / DataLibraryWindow.*.

## Verification
- Full `cmake --build .` clean (link of DolphinExplorer.exe + all test exes).
- App launches and stays up.

## Remaining (not done, by design)
- AppStyleDialogs.cpp (660) — single declarative QSS concern; left intact per scan.
- MainWindow.cpp (726) and MapView3D.GL.cpp (600) — composition root / single-concern
  GL TU; both intentionally excluded.
