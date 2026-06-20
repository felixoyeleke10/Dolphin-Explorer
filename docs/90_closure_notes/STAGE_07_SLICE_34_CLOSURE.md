# Stage 07 Slice 34 — Cross-modality Contact Manager

## Goal (user request)
"This app now needs a contact manager… add it to the app menu too… the contact manager
will be linked to SSS, SBP, MAG, MBES, etc." A first-class, cross-modality surface for
all contacts in a project regardless of the sensor they were picked on.

## What already existed (reused, not rebuilt)
- `core::Contact` (label, lat/lon, spatial_ref, depth/range, **line_id**, artifact_id,
  classification, **confidence**, notes, tags, group_id).
- `Project` contact CRUD (`addContact/updateContact/removeContact/contacts()`) +
  `ProjectEventBus` `contactAdded/contactRemoved/projectReplaced` relay.
- Undoable `AddContactCommand` / `RemoveContactCommand`.
- Per-contact detail editing in the shared Inspector (`InspectorPanel::showContact`).
- Modality enum already includes Sidescan/SubBottom/Magnetometer/Multibeam/Raster.
- Waterfall contacts already set `line_id` = the source layer id, so modality resolves
  via `findLayer(line_id)->modality`. (Map picks have no line_id.)
- NOTE: the older `ContactListPanel` was built but never instantiated (orphan); left in
  place, the new window supersedes it.

## New: `ui/features/contacts/ContactManagerWindow.{h,cpp}`
Standalone lazy window (mirrors the metadata-window pattern: `WA_DeleteOnClose`,
`setProject` + show/raise/activate). **Laid out like Windows File Explorer** (user
directive):
- **Left navigation tree ("folders")** — `Contacts → sensor (SSS/SBP/MAG/MBES/Raster) →
  individual line/source`, plus a `Map / unlinked` folder for map picks / unresolved
  `line_id`. Each node shows a `(count)`. Only non-empty sensor buckets appear; selection
  is preserved across rebuilds.
- **Right details list** (Explorer "Details" view): Label · Sensor · Line/Source · Class ·
  Confidence · Lat/N · Lon/E · Depth · Range. Sortable columns. Sensor + source resolved
  per row from `Contact::line_id` → `DataLayer`.
- **Address bar / breadcrumb** ("Contacts › Sidescan (SSS) › Line01") + a **search** box
  (label / class / notes) that hides non-matching rows in the current folder.
- `QSplitter` between tree and list; status footer shows "N of M contacts" + Remove/Export.
- Signals: `contactActivated` (row double-click / Go to / Properties), `removeContactRequested`,
  `exportContactsRequested`. Selecting a tree folder filters the list.

### Explorer command bar (Windows 11-style, top of window)
A `QToolBar` command surface; **every command is functional (D-05 — no stubs)**:
- **Cut / Copy / Paste** — operate on whole contacts via an internal clipboard. Copy also
  writes TSV to the system clipboard (paste into Excel). Paste re-adds clipboard contacts
  as new picks (project assigns ids; "… copy" suffix); Cut + Paste = move (removes originals).
- **Rename** (F2) — `QInputDialog` → `Project::updateContact`.
- **Delete** (Del) — multi-select; routes to the undoable `RemoveContactCommand` via
  `removeContactRequested`. Optional "Confirm before delete" (Options).
- **Sort ▾** — Ascending/Descending + by Label/Sensor/Source/Class/Confidence/Depth/Range.
- **View ▾** — per-column visibility toggles (`layout_customize` icon).
- **★ Favourite** — toggles a `favourite` tag (`Contact::tags`); favourited rows show a ★
  and populate a top-level **★ Favourites** quick-access folder in the tree.
- **More … (overflow)** — Select All (Ctrl+A) / Select None / Invert Selection · Properties
  (opens the Inspector) · **Options** (Show Map/unlinked folder; Confirm before delete).
- Table is multi-select (ExtendedSelection); command enabled-state tracks selection +
  clipboard. Shortcuts (Cut/Copy/Paste/Rename/Delete/Select-All) fire while focused.

Note: Rename/Favourite/Paste/Cut mutate the project directly (auto-refresh via the
ProjectEventBus contactAdded/Removed; Rename/Favourite use `updateContact` + local
`refresh()`); only Delete is routed through the undo stack (matches the prior behaviour).

### Visual redesign (user: first cut was "low effort and unsmart")
Rebuilt as a proper `QMainWindow` styled through the app design system (AppStyleDialogs.cpp
QSS for `#contactManagerWindow`, `#contactCmdBar`, `#contactNavTree`, `#contactBreadcrumb`,
`#contactSearch`, `#contactPreview` + labels, status bar):
- **Three-pane layout**: nav tree │ details list │ **preview/details pane**.
- **Colour-coded chips**: a `ChipDelegate` paints the Sensor column as a rounded sensor-
  coloured chip (reusing the node-graph category palette) and the Confidence column as a
  pill with a status dot (Possible/Probable/Certain → grey/amber/green).
- **Preview pane** (Explorer-style): title (★ when favourite), sensor + confidence chips,
  Class / Line / Position / Depth / Range form, Notes, Created/Modified + Tags, and a
  "Go to on Map" button. Shows a placeholder when 0 or >1 rows are selected.
- **Clickable breadcrumb** (rich-text links navigate the tree), command bar with hover
  states, status bar showing "N contacts · M selected", grid off, custom row height.
- Export moved into the command bar; the old footer Remove/Export buttons were dropped.

### Details ↔ Thumbnails view switching (user: "show the way jpg shows in Explorer")
The centre pane is a `QStackedWidget` with two synced views over the same filtered set:
- **Details** — the colour-chip table (default).
- **Thumbnails** — a `QListWidget` in IconMode; each contact is an Explorer-style tile
  drawn by `makeContactThumb()` (rounded sensor-coloured card + map-pin glyph + sensor
  tag + confidence dot + ★ when favourited), label beneath.
- **Switcher**: Explorer-style toggle buttons (☰ / ▦) at the bottom-right of the status
  bar, plus View ▸ Layout ▸ Details / Thumbnails. `setViewMode()` carries the current
  selection across the switch.
- Selection / search / status / preview / commands / context menu are all view-aware
  (read from whichever view is active). QSS: `#contactThumbs`, `#contactViewBtn`.

## Wiring (MainWindow)
- `onContactManagerOpen()` (in MainWindow.WaterfallCoordinator.cpp): creates the window,
  connects `contactActivated → onContactSelected` (map highlight + Inspector for edit),
  `removeContactRequested →` undoable `RemoveContactCommand`, `exportContactsRequested →
  onExportCsv`. Live refresh via the **ProjectEventBus** (contactAdded/Removed → refresh,
  projectReplaced → setProject), connected with the window as context so they die with it.
- Member `QPointer<QWidget> m_contact_mgr_win` (lazy, like `m_metadata_win`).
- **Entry points** (gated by `Features::kContacts`, per user): a **toolbar icon**
  (`contacts.svg`) in the top toolbar next to Data Library, and **Project → Contacts →
  Contact Manager…**. (An earlier View-menu entry was removed at the user's request.)

## D-05 compliance
The window is fully functional (list/filter/search/navigate/remove/export) — no clickable
stubs. Export reuses the existing CSV export.

## Build
`dolphin-ui-contacts` (new source added to CMake) + `dolphin-ui-mainwindow` +
`DolphinExplorer.exe` compile + link clean.

## Runtime verification (manual)
- Open from View → Contact Manager… (and Project → Contacts → Contact Manager…).
- Pick contacts in the waterfall (SSS/SBP) and on the map → appear with correct Sensor tag.
- Filter by sensor + search; click a row → map highlights + Inspector shows it.
- Remove (button / context menu) → row disappears, undo restores it; Export… writes CSV.

## Possible follow-ups (not in this slice)
- Inline edit of Label/Class/Confidence in the table (currently via Inspector).
- Group/tag columns; per-modality colour chips; double-click to open the owning viewer.
- Set MAG/MBES `line_id` on their contact picks once those pick flows exist.
