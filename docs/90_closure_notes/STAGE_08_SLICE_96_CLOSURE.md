# Stage 08 — Slice 96 Closure: Layer visibility through DisplayStateManager

## Goal
S-96: close the LAST direct display-state write in the UI layer. Layer
visibility was mutated inside `onLayerVisibilityChanged`'s undo apply-lambda
(`layer->visible = v` + hand fan-out to 4 widgets), bypassing the
DisplayStateManager authority every other display aspect already goes through.

## Changes
- `MainWindow.Layout.cpp` — the undo command's apply-lambda is now one line:
  `m_display_state->setLayerVisible(lid, v)`. The setter writes the model and
  emits `displayStateChanged(lid, Visibility)`.
- `MainWindow.cpp` — the existing displayStateChanged bus handler gains the
  Visibility branch: fans out to viewport host / map view / line list / layer
  picker, reading the truth back from the model. The pre-existing "any
  per-layer change marks the project dirty" rule covers persistence.
- Undo/redo replay through the same setter, so they land in the same fan-out —
  there is now exactly one sync point for visibility.

## Verification
- Grep-clean: no `DataLayer` display-field writes remain in `src/ui` outside
  `DisplayStateManager.cpp` (the one `ld.visible = true` hit is LayerMapData,
  a render struct, not the model).
- In-app (temp diag, SBP-Only project): toggle→hide sets model=0 map=0
  dirty=1; undo restores model=1 map=1; redo hides again; final undo restores.
  Diag removed after capture.
- Full suite 16/16 green.

## Notes
The no-project branch of `onLayerVisibilityChanged` still pokes the viewport
directly — correct, since there is no model to route through.
