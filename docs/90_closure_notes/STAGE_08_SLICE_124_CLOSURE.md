# Stage 08 Slice 124 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-124 — deterministic file-tree selection painting
- primary goal: remove the duplicate native/QSS selection highlights from
  indented rows in the file explorer tree

## What Changed

- `LayerTreeWidget::drawRow` clears selected and hover state before Qt paints
  the hierarchy branches.
- `LayerTreeItemDelegate` draws one rounded accent shape in Qt's content-only
  item rectangle, which begins after the reserved hierarchy gutter.
- `LayerTreeWidget::drawBranches` no longer invokes Qt's native/style-sheet
  branch primitive; it paints only the disclosure triangle, so no branch code
  remains that can fill indentation slots from selection-model state.
- This prevents Qt/QSS from treating every reserved indentation/expand-collapse
  segment as a separately selected rounded item.
- Selected text uses the theme's mode-aware primary-text colour.
- The behavior is local to the file explorer tree and does not change the
  selection appearance of unrelated tree or list widgets.

## Files Touched

- `src/ui/shared/panels/LineListPanel.cpp`
- `docs/90_closure_notes/STAGE_08_SLICE_124_CLOSURE.md`

## Tests Or Validation

- `dolphin-ui-shared` compiled successfully.
- After closing the running application that held the executable open, the
  full build and `DolphinExplorer.exe` relink completed successfully.
- `git diff --check` passed.
- Source inspection confirms selected state is removed before item rendering
  and the native branch renderer is bypassed, leaving one explicit selection
  painter.

## Gate Status

- gate items completed: file-tree selection has one deterministic painter
- gate items still open: none for this bounded UI hardening slice

## Risks / Follow-Ups

- The selection painter uses the shared accent, radius, and mode-aware text
  theme tokens; changes to those tokens continue to reskin this tree.

## What The Next Stage May Assume

- Indented file-tree rows no longer receive a second native full-row or
  decoration-area selection highlight.
