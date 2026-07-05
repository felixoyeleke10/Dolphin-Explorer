# Stage 07 — Slice 94 Closure: Views panel / right panel modality parity

## Goal
Two symmetry fixes requested from one screenshot (empty strip red-boxed in the
right panel + "left Views should behave like the right panel's sensor tabs"):

1. The right panel's lower sensor shell (SSS | SBP | MAG tab strip) rendered as
   an empty strip when the project had no sensor layers (or no project at all).
   D-05: no dead surface — hide it until a sensor modality exists.
2. The left **Views** panel always showed MAP | SSS | SBP tabs regardless of
   project content. It now mirrors the right panel: only modalities the project
   contains are offered, and the tab follows the active layer's modality.

## Changes

- `MainWindow.h` — new member `m_sensor_shell` (right panel lower shell);
  `refreshViewsPanel(bool follow_active = false)`.
- `MainWindow.MainArea.cpp` — capture `m_sensor_shell = lower;` when building
  the right-panel splitter.
- `coordinators/MainWindow.LayerCoordinator.cpp` —
  `refreshInspectorModalities()` also sets
  `m_sensor_shell->setVisible(has_sss || has_sbp || has_mag)`;
  `onLayerSelected` now calls `refreshViewsPanel(true)` so the Views tab
  follows the selected layer's modality.
- `panels/ViewsPanel.{h,cpp}` — new API:
  - `setModalities(bool has_sss, bool has_sbp)` — hides/disables the SSS/SBP
    tab buttons (MAP always available); falls back to MAP if the current tab
    disappears.
  - `setCurrentTab(int)` — 0 MAP / 1 SSS / 2 SBP, guarded against hidden tabs.
- `MainWindow.ContextPanels.cpp` — `refreshViewsPanel(follow_active)` scans
  `currentProject()->layers()` for present modalities → `setModalities`, and
  only when `follow_active` (layer-selection path) snaps the tab to the active
  layer's modality. Palette/quality-triggered refreshes deliberately do NOT
  yank the user off a tab they browsed to.

## Verification (in-app, temp diag since VM display < app minimum)

- No project: `sensor_shell_visible=0`; right panel shows Properties page only,
  no stray strip. Views panel shows a single MAP tab.
- SBP-only project open: `sensor_shell_visible=1`; Views panel shows
  MAP | SBP (SSS hidden), SBP tab auto-selected with the layer's palette
  (Inverted Grey) and display params populated.
- Build clean, ctest 16/16 green. Temp diag removed.

## Notes
- Auto-follow is selection-driven only, by design — display-state refreshes
  (palette, quality) keep the user's manually chosen tab.
- MAG has no Views page yet; when magnetometer display controls arrive, extend
  `setModalities` with a third flag.
