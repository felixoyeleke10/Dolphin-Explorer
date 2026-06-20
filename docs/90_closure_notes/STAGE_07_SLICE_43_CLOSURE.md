# Stage 07 — Slice 43: Adaptive right Properties panel height

## Goal
The right Properties panel's upper shell (Properties / Chats / History) reserved a
fixed ~320 px from the vertical splitter regardless of content. A short property
list (e.g. an SSS layer's ~6-row Info section) left a large dead gap between the
content and the lower sensor tab bar (SSS / Map). Make the upper pane *adaptive* —
size it to its current content — while still scrolling when content is too long.

## Change
- **`InspectorPanel`** — added `int contentHeight() const`, returning the
  *currently shown* page's `sizeHint().height()` (layer / contact / empty), not the
  max over all stacked pages. Lets the host pane hug the real content.
- **`MainWindow::adjustPropsSplit()`** (new) — sizes the props vertical splitter:
  - Properties tab: upper = tab-bar header height + `m_inspector->contentHeight()`
    + small chrome.
  - Chats / History: upper = 60 % of available (those tabs need room).
  - Clamped so the lower sensor shell keeps a ≥160 px floor and the upper a ≥64 px
    floor; degenerate (very short) panels fall back to a 50/50 split.
  - Splitter stretch factors set to upper=0 / lower=1 so the lower shell absorbs
    all slack — no gap under short lists; long content scrolls in the upper pane's
    own scroll area.
- Call sites: after first layout (`QTimer::singleShot(0)` in `buildPropertiesPanel`),
  on `resizeEvent`, on `onPropsTabChanged`, after a layer is selected
  (`LayerCoordinator`), and after a contact is selected (`ContactCoordinator`).
- Stored the splitter as `m_props_splitter`; forward-declared `QSplitter` in the
  header; added missing `<QStackedWidget>` / `<QToolButton>` includes to
  `ContactCoordinator`.

## Files
- `src/ui/mainwindow/panels/InspectorPanel.{h,cpp}`
- `src/ui/mainwindow/MainWindow.{h,MainArea.cpp,Layout.cpp}`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp`
- `src/ui/mainwindow/coordinators/MainWindow.ContactCoordinator.cpp`

## Verification
- Full MSVC/Ninja build green.
- Upper pane now collapses to the Info section's height; sensor tab bar rides up
  directly beneath it. Switching to Chats/History expands the pane; selecting a
  taller contact card re-fits. Overlong content scrolls within the upper pane.

## Notes / follow-ups
- Auto-fit re-runs on selection/tab-change/resize, so a manual splitter drag is
  overridden on the next such event. Acceptable per the "adaptive" directive; a
  future slice could remember a user drag if desired.
