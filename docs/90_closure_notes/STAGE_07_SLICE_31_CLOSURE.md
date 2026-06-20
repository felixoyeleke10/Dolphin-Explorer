# Stage 07 Slice 31 — DisplayStateManager (slice 4: SBP gain/signal/display)

## Goal
Route the per-layer **SBP display state** (gain, signal, display params + palette)
through `DisplayStateManager` so it is the single mutate point + notifier, matching the
slice-1/3 treatment of map quality and the global map palette. Previously the
SubBottomCoordinator wrote `layer->sbp_display_state.*` directly and called
`markProjectDirty()` inline in five places.

## What changed
- **`DisplayStateManager`** gained per-layer SBP setters (coordinator over
  `DataLayer::sbp_display_state`):
  - `setLayerSbpGain(layer_id, SbpGainParams)` — writes gain + `gain_customized`, emits
    `displayStateChanged(layer_id, Gain)`.
  - `setLayerSbpSignal(layer_id, SbpSignalParams)` — writes signal + `signal_customized`,
    emits `Gain` (the Gain aspect covers gain/contrast/signal per the enum).
  - `setLayerSbpDisplay(layer_id, SubBottomDisplayParams)` — writes display +
    `display_customized` + `sbp_palette`, emits `Palette`.
  - `setAllSbpGain` / `setAllSbpSignal` — apply-to-all: loop every SBP layer in the
    project, set each (emits per layer so each is marked dirty).
  - Forward-declares the param structs in the header; the `.cpp` gets the full
    definitions via the already-included `DataLayer.h`.
- **`MainWindow.SubBottomCoordinator`** now funnels every SBP display mutation through
  the manager instead of writing the model + `markProjectDirty()` inline:
  - Settings-dialog palette → `setLayerSbpPalette`.
  - Right-panel Display module → `setLayerSbpDisplay`.
  - Gain module apply-to-line/all → `setLayerSbpGain` / `setAllSbpGain`.
  - Signal module apply-to-line/all → `setLayerSbpSignal` / `setAllSbpSignal`.
  - The live SBP-window refresh (`applyGainParams`/`applySignalParams`/
    `applyDisplayParams`) stays inline in the coordinator — it holds the exact params and
    guards on the current layer; a single repaint, no read-back from the model.

## Dirty tracking
No more inline `markProjectDirty()` in these handlers — the existing MainWindow
`displayStateChanged` bus handler marks the project dirty on any non-empty `layer_id`,
so each per-layer emit covers it (apply-to-all emits per layer → still marked dirty).

## Build
`dolphin-ui-systems`, `dolphin-ui-map`, `dolphin-ui-mainwindow` compile + link clean.

## Runtime verification (manual)
- Change SBP gain / signal / display params (line + all) → SBP window updates live.
- Reopen the project → the per-layer SBP look persists (already serialized).

## Remaining DSM migration (next slices)
3. Nav overlay choices — through DSM (NavOverlay).
4. Per-view: 3D settings + waterfall view params owned by DSM (ThreeD/WaterfallView).
5. Finish global-default bridging (background, coord format) so all views read the DSM.
