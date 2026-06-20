# Stage 07 Slice 33 — DisplayStateManager (slice 6: SSS per-layer display params)

## Goal
Route the per-layer **SSS display state** (the full waterfall params: gain / contrast /
channel / palette) through `DisplayStateManager` so it is the single mutate point +
notifier, matching SBP gain/signal and nav. Previously two handlers in the
WaterfallCoordinator wrote `layer->sss_display_state.{params,customized}` directly.

## What changed
- **`DisplayStateManager`** gained `setLayerSssDisplay(layer_id, WaterfallParams)` —
  writes `DataLayer::sss_display_state.params` + `customized = true`, emits
  `displayStateChanged(layer_id, Gain)`. Forward-declares `WaterfallParams`; full type via
  `DataLayer.h` → `SssDisplayState.h`.
- **`MainWindow.WaterfallCoordinator`**:
  - `paramsApplied` (single, the waterfall's current line) → `setLayerSssDisplay`; the
    inline `markProjectDirty()` is gone (the bus handler marks dirty per non-empty
    layer_id).
  - `applyToAllRequested` loop → `setLayerSssDisplay` per layer; the
    `slant_range_corrected` write + `reloadCurrentLayer` + `ProjectTransaction` stay inline
    (slant-range correction is data-affecting/reload-triggering, not pure display look).

## Scope boundary
The map's live colour sync (`m_sss_ctrl->setDisplayParams(map_dp)` with
`map_dp.palette = m_display_state->mapPalette()`) and the slant-range
reload/bake path are untouched — this slice only moves the per-layer
`sss_display_state` persistence/notification into the authority.

## Dirty tracking
The existing MainWindow `displayStateChanged` bus handler marks the project dirty on any
non-empty `layer_id`, covering both the single and the per-layer apply-to-all emits. The
apply-to-all path also still commits its `ProjectTransaction`.

## Build
`dolphin-ui-systems` + `dolphin-ui-mainwindow` compile + link clean (serial build to avoid
the parallel /FS PDB-lock contention; see SLICE_32 note).

## Runtime verification (manual)
- Apply gain/contrast/channel in the waterfall (line + all) → waterfall + map update;
  reopen the project → the per-layer SSS look persists.

## Remaining DSM migration (next slices)
- SSS per-layer palette/channel fine-grained aspects (currently folded into the Gain
  apply + the separate `setLayerSssPalette`).
- Per-view: 3D settings + waterfall view params owned by DSM (ThreeD/WaterfallView).
- Finish global-default bridging (background, coord format) so all views read the DSM.
