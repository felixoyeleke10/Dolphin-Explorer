# Stage 07 — Slice 53: Drag-to-reorder right-panel tool sections

## Goal
Let users drag the right-panel tool sections (Display, Radiometry, Imaging,
Navigation, Geometry, SBP tools…) into their preferred order, and remember it.

## Design
Reorder is coordinated by the host that owns the sections (`RightPanelHost`); the
section widget only reports the drag.

- **`CollapsibleSection`** — the header is now a drag handle:
  - `setReorderable(bool)` (open-hand cursor; closed-hand while dragging).
  - The header event filter distinguishes a click (toggle expand/collapse) from a
    drag (past `QApplication::startDragDistance()`), emitting `reorderStarted` /
    `reorderMoved(globalPos)` / `reorderFinished`. A plain click still toggles.
- **`RightPanelHost`** — owns the reorder + a thin accent drop-indicator line:
  - On drag-move it computes the insert slot from the pointer's Y among the
    currently-visible sections and positions the indicator at that gap.
  - On drop it moves the section in `m_sections`/`m_modules`, rebuilds the layout
    (`relayoutSections`), re-asserts modality-filter visibility, and persists.
  - Persistence: `QSettings` key `rightPanel/sectionOrder/<universal|modal>` stores
    an ordered list of module keys (`modality:title` — unique even for the
    per-modality Navigation/Geometry). `applySavedOrder()` (ctor) restores it;
    unknown/new modules are appended, and a section is never dropped.

Reordering operates on the full section list using visible neighbours as the drop
target, so it works correctly even though only the active sensor's sections are
shown (the others are hidden by the modality filter).

## Files
- `src/ui/shared/widgets/CollapsibleSection.{h,cpp}`
- `src/ui/mainwindow/rightpanel/RightPanelHost.{h,cpp}`

## Verification
- Build green.
- NEEDS VISUAL CHECK: drag a tool section's header up/down → drop indicator shows the
  target → section moves on release → order persists across project/app restart. A
  short click (no drag) still expands/collapses.

## Context menu (right-click the panel)
`RightPanelHost::contextMenuEvent` offers:
- **Expand All** / **Collapse All** — toggle every visible section.
- **Reset Section Order** — restore the construction order and clear the persisted
  order (disabled when already default). The construction order is snapshotted as
  `m_default_order` before `applySavedOrder()` in the ctor.

## Context-menu z-order fix
First cut used a `contextMenuEvent` override + `menu.exec`; on the frameless main
window (`FramelessWindowHint`) that made the app drop behind other windows on
right-click. Fixed by matching the app's proven pattern — `Qt::CustomContextMenu` +
`customContextMenuRequested` → `showContextMenu()` — and re-asserting the top-level
window's foreground (guarded `raise()`+`activateWindow()` only if it lost active)
after the menu closes.

## Note
Order is saved per host (`universal` = Info-only; `modal` = the sensor tools). It is a
single global order per host, not per-layer.
