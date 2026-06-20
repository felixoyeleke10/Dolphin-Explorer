# Stage 07 — Slice 42: Contact pick snapshot + waterfall Contact Manager button

## Goals
1. When the user places a contact pick in the waterfall, capture a **square
   screenshot** of that area and use it as the contact's thumbnail.
2. Add a **Contact Manager** button to the waterfall window's upper toolbar.

## 1. Pick snapshot

**Capture** — `WaterfallView::mousePressEvent` (contact-pick branch): grabs a square
region of the GL framebuffer centred on the click (`grabFramebuffer()` → centre square,
DPR-aware), *before* `update()` repaints so the new marker dot is not baked in. Added as
a `const QPixmap&` param on `WaterfallView::contactPicked` and `WaterfallWindow::contactCreated`.

**Persist** — `MainWindow::onWaterfallContactCreated` (ContactCoordinator) now receives the
pixmap and, after `AddContactCommand` assigns the id (new `AddContactCommand::assignedId()`),
saves it to `<project data>/contacts/<id>.png`.

**Storage model** — the snapshot is a **derived artifact keyed on the stable contact id**;
no field in `core::Contact` and no `.dlp` schema change. The Contact Manager loads it by id.
- `cmvis::contactSnapshotPath(project, id)` → the PNG path.
- `cmvis::contactThumbnail(project, contact, px)` → persisted snapshot (centre-cropped to a
  square, smooth-scaled) when present, else the synthetic `makeContactThumb` tile.
- Icon view uses `contactThumbnail`; the preview pane gained a square image
  (`m_pv_image`, hidden when no snapshot; QSS `#contactPreviewImage`).
- **Lifecycle**: permanent purge (`purgeSelection` / `emptyRecycleBin`) deletes the PNG so no
  orphans accumulate; project rename/move carries the file since it lives under `data/`.
  Recycle/restore keep it (id is stable).

## 2. Waterfall Contact Manager button
- `WaterfallWindow.Toolbar.cpp`: added a `:/icons/contacts.svg` button to the SSS right
  section + a "Contact Manager" command-palette entry; both emit the new
  `WaterfallWindow::contactManagerRequested`.
- `MainWindow.WaterfallCoordinator` wires `contactManagerRequested → onContactManagerOpen`.

## Compliance
- No band-aid: snapshot is a clean derived-artifact design (no model pollution, no schema
  change, id-keyed, with purge cleanup). Reuses the existing AddContactCommand/undo path.
- Layer rules intact; QPixmap stays in the ui layer (core::Contact unchanged).

## Verification
- Full `cmake --build .` clean (app + all test exes).
- App launches and stays up.

## Notes / possible follow-ups
- Snapshot side is fixed at 160 logical px; could be a setting later.
- Map-pick contacts (`onContactPicked`) don't create contacts, so no snapshot path there.
