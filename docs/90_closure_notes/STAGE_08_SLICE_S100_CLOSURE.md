# Stage 08 Slice S-100 Closure — Explorer/Views Splitter

## What changed

- Replaced the fixed File Explorer/Views boundary with a vertical splitter.
- Kept both panes non-collapsible and provided practical initial sizing.
- Persisted the divider position in application settings.
- Kept Views and Recycle Bin grouped in the lower pane.

## Files touched

- `src/ui/mainwindow/MainWindow.ContextPanels.cpp`

## Validation

- Incremental MSVC/Ninja compilation and main-window UI-library link.
- Diff whitespace validation.

## Remaining risk

- None specific to this slice.

## Stage gate

- This closes the sidebar-resizing request only; other Stage 08 criteria are unchanged.
