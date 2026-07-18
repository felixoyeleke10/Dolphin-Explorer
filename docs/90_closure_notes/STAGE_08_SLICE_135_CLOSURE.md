# Stage 08 Slice 135 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-135 — remove the File Explorer vertical-size cap
- primary goal: allow the File Explorer pane to use essentially the full left
  sidebar when the user drags the Explorer/Views divider downward

## What Changed

- The File Explorer remains non-collapsible so it always has a usable surface.
- The lower Views/display pane now has an independent vertical scroll viewport,
  so its full
  content height no longer caps the File Explorer pane.
- Both splitter children remain present; the lower pane retains a compact floor
  containing its section headers and scrolls the controls when space is tight.
- Existing splitter-position persistence remains unchanged; the user's chosen
  layout is still restored on the next launch. The key is versioned to V2 so
  old cramped splitter state does not override the larger new default.
- The new default split is 520/150 in favour of File Explorer.
- The lower-pane layout has no trailing spacer: Views absorbs its available
  height and Recycle Bin remains pinned to the bottom edge instead of floating
  above unused space.
- Recycle Bin is outside the Views scroll viewport, so dragging or scrolling
  Views cannot carry the bin downward; it remains a fixed bottom sibling.

## Files Touched

- `src/ui/mainwindow/MainWindow.ContextPanels.cpp`

## Tests Or Validation

- Compile validation of the `dolphin-ui` target.
- Manual interaction remains the definitive check for splitter drag range;
  Stage 08 deliberately defers QTest widget-driving infrastructure.

## Risks / Follow-Ups

- Views may show a vertical scrollbar when the lower pane is compact.
- This slice does not alter Views content, display-state behavior, or sidebar
  width.
