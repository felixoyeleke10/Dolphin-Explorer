# Stage 07 · Slice 79 — Contact editor from the viewers (waterfall + SBP parity)

## Goal
Expose the "Edit contact details" editor (slice 76) from the SSS waterfall
viewer — an Edit button plus direct marker access — with **identical design on
the SBP viewer** (SBP/SSS parity directive) and the main window.

## What changed

### Shared editor, hosted once by MainWindow
`MainWindow::onContactEditRequested(uint64_t id, const QString& line_id)`
(ContactCoordinator) opens ONE shared `ContactEditorDialog`:
- Scope: the given line's contacts (viewer entry points), else the focused
  contact's line, else all project contacts. Prev/Next walks that scope.
- Wiring: save → `UpdateContactCommand`, delete → `RecycleContactCommand`
  (both on the undo stack), stepping → `onContactSelected` (map/inspector
  sync), export → single-contact report, bus add/update/remove/projectReplaced
  → editor refresh. The Contact Manager keeps its own editor instance (scoped
  to its visible folder); both stay in sync through the bus.

### Shared interactive export
The manager's format-dialog + write flow moved to
`ContactReport::exportInteractive(parent, title, rows, project)`; the manager's
`exportContactSet` is now a thin wrapper and the MainWindow-hosted editor uses
the same helper — every export entry point behaves identically.

### Waterfall (SSS)
- `WfContact` gained the project contact `id`; `refreshExternalContacts` fills it.
- Marker-position math extracted to `WaterfallOverlayPainter::contactPixelPos`
  (paint + hit-test share one implementation).
- **Double-click a contact diamond** (no tool active) → opens the editor on
  that contact (12 px hit radius).
- **"Edit Contacts…" button** in the Contact Picking section → editor over
  this line's contacts.

### SBP — full parity
- `SubBottomView` gained a **contact marker overlay** (it previously showed no
  contacts at all): `ContactMark {trace_idx, depth_s, id}`,
  `setExternalContacts`, `paintContactMarks` — same visual as the waterfall
  (dark-halo diamond + Theme::kWarning fill) — plus `contactMarkPixelPos` and
  the same **double-click → editor** hit-test.
- `SubBottomWindow::setProjectContacts` + `refreshContactOverlay` filter to the
  loaded line and convert `depth_m → depth_s` via half sound-speed; the overlay
  re-derives on trace load and on sound-speed change.
- The shared `ContactPickingPanel` (SBP + main window) gained the same
  **"Edit Contacts…"** button; SBP forwards line-scoped, the main window opens
  the editor over all contacts.

### Bus sync hardening
The viewer-overlay resync in MainWindow now covers **contactUpdated** (was
add/remove only — edits from the manager/editor previously left stale waterfall
markers) and feeds **both** viewers; the SBP open path seeds
`setProjectContacts` like the waterfall's.

## Toolbar round (follow-up)
The Edit entry point was too buried in the panel section — added a visible
**Edit Contact Details toolbar button** to BOTH viewer toolbars (new
`contact_edit.svg` icon: contact target + pencil, in the house icon style),
placed with the other contact buttons in the waterfall and next to the tools
in the SBP viewer. Also added an **"Edit Contact Details"** command to both
viewers' command palettes.

## Verification
Full rebuild green (MSVC + Ninja); `ctest -E PerfBaseline` → 16/16 passed.

## Notes
- Waterfall markers double-map unlinked (map-picked) contacts to both channels
  at the nearest nav row — double-click resolves to the same contact id either
  way, so behaviour is correct.
- SBP markers require the contact's `line_id` to match the loaded line
  (SBP picks always set it); unlinked map contacts are not nav-matched onto the
  section yet — noted as possible follow-up.
