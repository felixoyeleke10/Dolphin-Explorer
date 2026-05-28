# Stage 01 Slice 01A Closure

## Scope

- active stage: `Stage 01`
- active slice: `01A`
- primary goal: unify `MainWindow` shell-state transitions so panel, workspace, properties, and toolbar changes no longer bypass the state model

## What Changed

- routed workspace application through one shell-state path instead of mixing direct widget visibility edits with animation state
- assigned the built right toolbar back to `m_right_tool_bar` so customization and visibility logic operate on the real widget
- added explicit helpers for:
  - workspace application
  - properties-panel visibility/state
  - right-toolbar visibility
  - panel normalization
- made `PanelExplorer` behave as the survey workspace that closes the context panel instead of opening an empty overlay page
- updated properties-panel geometry calculations so hiding the right toolbar no longer leaves a dead gutter

## Files Touched

- `src/ui/MainWindow.h`
- `src/ui/MainWindow.cpp`

## Tests Or Validation

- direct `g++ -fsyntax-only` check for `src/ui/MainWindow.cpp` passed
- direct `g++ -c` compile invocation for `src/ui/MainWindow.cpp` returned success
- searched for remaining direct workspace/property visibility bypasses in `MainWindow.cpp`

## Gate Status

- gate items completed:
  - one real shell-state path for panel/workspace transitions
  - right-toolbar member/state mismatch removed
- gate items still open:
  - later Stage 01 slices for artifact-store/session, metadata parity, and activation/loading

## Risks / Follow-Ups

- full app link/build verification is still blocked while `build_mingw\\DolphinExplorer.exe` is running
- workspace menu checkmarks are still lightweight UI state, not a dedicated workspace policy object

## What The Next Stage May Assume

- workspace customization no longer needs to call `setVisible(...)` directly on shell widgets
- `PanelExplorer` is the “close context panel / survey workspace” state
- right-toolbar visibility can be treated as real shell state instead of a dead member pointer
