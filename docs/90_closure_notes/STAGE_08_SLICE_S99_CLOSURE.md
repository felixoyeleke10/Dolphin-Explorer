# Stage 08 Slice S-99 Closure — Universal SSS Palette

## What changed

- Routed the Views panel SSS palette picker through the existing global palette authority.
- Applied global palette notifications to an already-open waterfall as well as the map renderer and shared inspector.
- Made Views panel refreshes read the global palette instead of the legacy per-layer palette field.
- Updated the SSS palette tooltip to describe its universal scope.

## Files touched

- `src/ui/mainwindow/MainWindow.ContextPanels.cpp`
- `src/ui/mainwindow/MainWindow.Runtime.cpp`
- `src/ui/mainwindow/panels/ViewsPanel.cpp`

## Validation

- Incremental MSVC/Ninja compilation and `dolphin-ui-mainwindow` link succeeded.
  Final executable relink was blocked by a running `DolphinExplorer.exe`
  (`LNK1168`); the changed translation units produced no compile errors.
- Static trace of all SSS palette entry points through `MainWindow::onPaletteChanged` and `DisplayStateManager::setMapPalette`.

## Remaining risk

- Legacy per-layer `sss_palette` values remain serialized for project compatibility, but no longer drive the universal Views control.

## Stage gate

- This closes the palette propagation defect only; the remaining Stage 08 exit criteria are unchanged.
