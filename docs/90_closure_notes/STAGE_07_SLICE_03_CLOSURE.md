# Stage 07 Slice 03 Closure — Unified Side-Panel Chrome System

## Goal
Eliminate visual drift between the left (File Explorer) and right (Properties)
dock panels. Before this slice there was no shared panel-chrome component: each
panel — and each of the two tab strips inside the Properties panel — was built
from bespoke inline widget code with its own object names. Two strips that were
meant to look identical (`propsTabs` and `sensorTabBar`) were already drifting
(one had a bottom border, the other a top border; the upper one's border was
silently cancelled by a later `border: none`).

## Architecture

Two small shared widgets now own the chrome, and one shared QSS contract styles
them. The left dock and both panes of the right dock are assembled from the same
pieces, so they cannot drift again.

```
SidePanelShell  (QFrame#sidePanelShell)
├── header  (stretch 0) — a PanelTabBar or a custom title QFrame
└── body    (stretch 1) — arbitrary content

PanelTabBar  (QWidget#panelTabBar)
├── QToolButton#panelTab   (exclusive, checkable)
├── QFrame#panelTabSep     (1px divider, auto-inserted between tabs)
└── signal tabChanged(int)
```

Usage map:
- Left File Explorer dock → `SidePanelShell` { header = title `#panelHdr`,
  body = tree + collapsible sections }.
- Right Properties upper pane → `SidePanelShell` { header = `PanelTabBar`
  (Properties/Chats/History), body = `QStackedWidget` }.
- Right Properties lower pane → `SidePanelShell` { header = `PanelTabBar`
  (SSS/SBP/Map/MAG), body = modal scroll area }.

## New Components

### `src/ui/shared/widgets/PanelTabBar.{h,cpp}`
Fixed-height (`Theme::kPanelHdrH` = 38px) horizontal strip of mutually-exclusive
`QToolButton` tabs separated by 1px dividers.
- `addTab(label, id)` returns the `QToolButton*` so callers keep semantic
  pointers (e.g. `m_tab_sss`) for enable/disable and checked-state control.
- Owns the `QButtonGroup`; emits `tabChanged(int)` on user activation only
  (programmatic `setCurrentId` / `setChecked` does not emit).
- Auto-inserts a `panelTabSep` divider before every tab after the first.

### `src/ui/shared/widgets/SidePanelShell.{h,cpp}`
Thin `QFrame#sidePanelShell` with a zero-margin vertical layout. `setHeader()`
pins a fixed strip at the top; `setBody()` fills the remainder (stretch 1). Both
setters reparent and replace any previous widget, so the call order is
irrelevant.

## Shared QSS Contract (AppStylePanels.cpp)
Replaced the four per-bar rules (`propsTabs`, `propsPanelTab`, `propsTabSep`,
`sensorTabBar`) with one shared block keyed on `sidePanelShell`, `panelTabBar`,
`panelTab`, `panelTabSep`. Both tab strips now share a single bottom-border
treatment under the tabs (the splitter handle separates the two panes), removing
the prior top/bottom-border inconsistency.

## Wiring Changes
- `MainWindow.MainArea.cpp` — `buildPropertiesPanel()` rebuilt around two
  `SidePanelShell` panes + two `PanelTabBar`s. The upper bar's `tabChanged`
  connects directly to `onPropsTabChanged`; the sensor bar's `tabChanged`
  drives `refreshSensorTab`. Removed the inline `makeTab` lambda, the two inline
  `QButtonGroup`s, and the six manually-created `QFrame` separators.
- `MainWindow.ContextPanels.cpp` — `buildContextPanel()` page is now a
  `SidePanelShell` with the existing `#panelHdr` title as header and the tree +
  sections wrapped as the body.
- `MainWindow.Shell.cpp` — removed the three per-button `clicked` connections
  (now superseded by the single `PanelTabBar::tabChanged` signal).
- The `m_props_tab_*` and `m_tab_*` member pointers are unchanged in type and
  still populated (from `addTab`'s return), so `MainWindow.LayerCoordinator.cpp`
  needs no changes.

## Files Changed
- `src/ui/shared/widgets/PanelTabBar.h` (new)
- `src/ui/shared/widgets/PanelTabBar.cpp` (new)
- `src/ui/shared/widgets/SidePanelShell.h` (new)
- `src/ui/shared/widgets/SidePanelShell.cpp` (new)
- `src/ui/CMakeLists.txt` — register the two new sources
- `src/ui/shell/AppStylePanels.cpp` — shared chrome QSS contract
- `src/ui/mainwindow/MainWindow.MainArea.cpp` — Properties panel rebuilt on shells
- `src/ui/mainwindow/MainWindow.ContextPanels.cpp` — File Explorer dock on a shell
- `src/ui/mainwindow/MainWindow.Shell.cpp` — drop per-button tab connections

## Build Result
All six changed/new translation units compiled with **0 errors, 0 warnings**;
`dolphin-ui-style`, `dolphin-ui-shared`, and `dolphin-ui-mainwindow` static
libraries relinked cleanly. The final `DolphinExplorer.exe` relink was blocked
only by `LNK1168` (the app was running and holding the file) — an environmental
lock, not a build error. The relink completes once the running instance exits.
