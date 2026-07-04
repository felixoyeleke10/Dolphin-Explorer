# Stage 07 · Slice 76 — Contact Editor ("Edit contact details")

## Goal
Our contact UX was read-only: the docked inspector and the Contact Manager
preview showed labels only; the only edits were rename / favourite / group.
Benchmarked against the SonarWiz / SeaView **"Edit contact details"** editor,
we needed a proper modal editor: a full attribute form beside the picked source
image, with step-through navigation. Product direction (this slice): build the
**full modal editor** and extend the data model to the **full reference field
set**.

## What changed

### Data model — `core::Contact`
Added the fields the reference exposes that we did not store:
- `shadow_m` — acoustic shadow length
- `burial_depth_m` — depth of burial below seabed
- `height_not_measurable` — "Not measurable" flag (height indeterminate)
- `symbol` — map glyph id ("" = default per classification)
- `color_rgb` — 0xAARRGGBB display colour (0 = auto per classification)
- `use_for_report` — include in generated reports
- `length_m` — measured object length (**distinct from `range_m`**, which stays
  the pick's slant range; serialized as `object_length_m` because the legacy
  `length_m` JSON key historically held slant range and still maps to `range_m`
  on read)

Width/Height/Depth map to the existing `width_m` / `height_m` / `depth_m`;
Description = `notes`.

### Serialization
`Project.Serialization.cpp` (write) and `Project.Serialization.Read.cpp` (read)
handle all new keys. Reads default to 0/false for older `.dlp` files, so existing
projects open unchanged (back-compat verified by test).

### New editor
- **`ContactEditorDialog`** (`features/contacts/ContactEditorDialog.{h,cpp}`) —
  standard `QDialog` (same chrome as other modals). Left: scrollable form
  (Name · Symbol · Class · Colour · Position · Height + Not measurable · Shadow ·
  Width · Length · Depth · Burial depth · Confidence · Tags · Description ·
  Use for report). Right: the source-image pane with Scale / Rotation sliders and
  a "Show contact icon" toggle. Command row: Delete · Export · ‹ Prev / Next › ·
  Close, with a "Name (i of n)" caption.
  - Edits **auto-commit** as undoable diffs when navigating away, deleting, or
    closing — only when an editable field actually changed (`editableEqual`).
  - `refresh(project)` re-syncs after external mutations (drops removed ids,
    keeps the current contact, closes when the set empties).
- **`ContactSnapshotView`** (`.h/.cpp`) — the image viewer: fits the persisted
  snapshot, applies user zoom (wheel + slider, 25–400 %) and rotation
  (−180…180°) about centre, and draws a target marker at the pick (snapshot
  centre) tinted with the contact colour.

### Wiring
- Hosted in **ContactManagerWindow**: double-click a row/thumbnail (or the
  Properties action) opens/re-targets the editor over the current visible list,
  so Prev/Next walks the same order the operator sees.
- The editor's intents route through the manager's **existing undoable signals**
  — `contactSaveRequested → contactsEditRequested`, `removeContactRequested`,
  `exportRequested → exportContacts()`, `contactActivated` for map/preview sync.
  No new undo plumbing; edits land on MainWindow's undo stack as before.
- Extracted `visibleIdsInOrder()` (shared by the editor's nav list and
  `exportContacts()`).

## Verification
- Full app build green (MSVC + Ninja).
- Extended `test_contacts` with `testEditorFieldsRoundTrip` (all new fields
  save/open correctly). `ctest -E PerfBaseline` → 16/16 passed.

## QC round (post-build)
A lifecycle-flow QC pass found and fixed:
1. **Close/Esc lost edits** — `QDialog::accept()/reject()` bypass `closeEvent`,
   so `commitIfChanged()` never ran for the Close button or Esc. Moved the
   commit into a `done(int)` override — the single funnel for every close path.
2. **In-progress edits wiped** — `refresh()` unconditionally reloaded the form
   on *any* bus-driven manager refresh. Now it keeps the form untouched when the
   current contact's stored state still matches the loaded snapshot (change was
   elsewhere) and only reloads on a real external change (e.g. undo).
3. **Delete re-entrancy** — the delete handler's emit synchronously re-entered
   `refresh()` mid-handler. Now `m_loading` blocks the re-entrant refresh and the
   handler re-syncs once afterwards.
4. **Export scope** — the editor's Export exported the whole visible set. Split
   `exportContacts()` into a shared `exportContactSet(rows, title)`; the editor
   now exports just the contact being edited.
5. **range/length conflation** — the editor's "Length" was bound to `range_m`,
   which for SSS picks stores the pick's slant range; editing Length would have
   silently overwritten pick metadata. Added a distinct `length_m` field
   (serialized `object_length_m` to dodge the legacy key).
6. **Pen state leaks** (feature tools, slice 75 surface) — Escape / `clear()` /
   `setContactTool()` reset the draft points but not `m_feature_pen_down` in the
   SBP and waterfall views; a cancelled stroke could resume capturing. All reset
   sites now clear the pen flag (map already did via `cancelFeatureDraft`).
Also removed an inline `setStyleSheet` on the colour button (styling debt).

## Layout revision (reference-match round)
User review: the first layout didn't match the agreed SonarWiz/SeaView
reference. Rebuilt to match it:
- **Symbol + Color on one row**; **Depth + Confidence on one row**; dense
  form spacing with left-aligned labels.
- **Editable position**: Northing/Easting spin boxes (Lat/Lon for geographic
  contacts — rows re-label and re-format per CRS on load) with a live WGS84
  echo beside each field (`geo::normalizeNavForMap`, UTM-family CRSs; falls
  back to the CRS name when untransformable). Edits commit like any other
  field; an untouched position keeps its full stored precision (the spins
  display rounded values) and the equality tolerance matches the spin
  resolution so navigation never produces phantom commits.
- **Tags** = editable combo (project-wide suggestions) + add/clear buttons +
  a tag list (double-click removes) — replaces the comma-separated line edit.
- **Image pane** = "Source image:" header (line + picked channel caption) with
  the **Export** button moved up beside it; footer row with the position
  readout (projected + WGS84) and the **"Show / hide contact icon"** toggle;
  **Scale + Rotation on one row** underneath.
- Command row is now Delete · ‹ Prev (n of m) Next › · Close.

## Visual redesign round (root cause: unstyled widgets)
User verdict on the rendered dialog: not acceptable. Root cause: the app's
QSS only styles widgets by object name, and the editor used naked
QDoubleSpinBox/QComboBox/QLineEdit/QListWidget — they rendered as stock
Windows-looking controls on the dark theme ("dangerBtn" had no stylesheet at
all). Fix at the system level, not inline stylesheets:
- **New `ce*` design section in `AppStyleDialogs.cpp`** (the styling authority):
  shared card treatment for text/numeric/combo/notes fields with focus accents,
  themed spin arrows (spin_up/spin_down glyphs), combo popover, tags list with
  hover/selected states, mini add/clear buttons, colour swatch button, framed
  image card (`#ceImageFrame`), Prev/Next nav buttons, a proper **danger Delete**
  treatment, and an Export button — all from theme tokens.
- **Structure**: dropped the QScrollArea (dialog sized to fit); the form is now
  grouped under muted section headers with hairline dividers —
  IDENTIFICATION (Name / Symbol+Color / Class+Confidence), POSITION
  (Northing+echo / Easting+echo), DIMENSIONS (Height+Not measurable /
  Width+Length / Shadow+Burial / Depth), NOTES (Tags / Description /
  Use for report). Width+Length and Shadow+Burial pair up per row to halve the
  vertical footprint.
- Close is the accent primary button (`#dlgBtnOk`); the dialog background and
  every label routes through the `contactEditor` / `ce*` QSS namespace.

## Polish round 2 (from rendered-screenshot critique)
- **White native title bars, app-wide** — the app never opted into DWM dark
  frames, so every non-frameless window (this dialog, viewers, the manager,
  settings) wore a white Windows title bar over the dark theme. New
  `ui/shell/WindowChrome.h` → `applyDarkTitleBar()` (DWMWA_USE_IMMERSIVE_DARK_MODE,
  20→19 fallback), applied automatically to EVERY top-level window on first
  show via the existing qApp event filter in MainWindow — one fix, all windows.
- **Colour button** was a red chip with "Auto" text jammed on it → now a pure
  swatch (chip fills the button; value/mode lives in the tooltip).
- **DIMENSIONS grid** — all spins share one fixed width and the second-column
  labels are fixed/right-aligned, so Width|Length and Shadow|Burial align into
  real columns; Depth sits in the same first column.
- **Spin steppers** — value text kept clear of the arrows (padding-right) with
  explicit top-right/bottom-right subcontrol placement.
- **Snapshot empty state** — a faint target glyph + "No source image" +
  one-line explanation (waterfall picks capture snapshots; map/SBP picks don't)
  instead of a bare string in a black void.
- **Tags list auto-hides when empty**; Description grew to 72 px.

## Polish round 3 (fetch-from-source + layout)
- **"Why is it not fetching the source image?"** — snapshots were only ever
  grabbed live from the GL framebuffer at pick time; contacts picked before
  that feature (or whose grab failed) had no PNG and the editor showed the
  empty state forever. New **fetch-from-source fallback**:
  `MainWindow::fetchContactSnapshot` loads a bounded ping window around the
  pick from the parsed-artifact cache (`ImportService::loadSidescanWindow`,
  index-first — never a full-file decode), renders a 160 px grayscale patch of
  the picked channel around the pick range (2–98 % percentile stretch, port
  mirrored to match waterfall orientation), **persists it** to the standard
  snapshot path (so manager thumbnails benefit too), and returns it. The
  editor calls the provider whenever the persisted PNG is missing; wired for
  both the MainWindow-hosted editor and the Contact Manager's (provider
  passthrough — the manager doesn't own an ImportService).
- **Left column narrowed** (292–318 px) — the source image is the dominant
  element again; dimension pair-rows resized to keep the two-column grid.
- **Source caption shows the source FILE name** (e.g. `C18_15.XTF · Port`),
  resolved layer → source → basename — never the internal layer id.

## Notes / follow-ups
- `symbol` / `color_rgb` are stored and edited but not yet honoured by the map /
  3D contact renderers — wiring them into `ContactVisuals` rendering is a natural
  next slice.
- The measurement line seen in the reference isn't drawn (we don't store the
  measured endpoints); the target ring marks the pick instead. Honest until we
  capture measurement geometry.
