# Stage 07 Slice 35 — Contact recycle bin + reactive contacts + dead-code purge

Driven by a QC pass ("any band-aids? we fix systems, not symptoms").

## 1. Root-cause fix — reactive contact updates (was a band-aid)
`Project::updateContact` / group ops emitted only the coarse `modified()` /
`contactGroupsChanged()`, with no precise per-contact signal — so the Contact Manager
patched around it by calling `refresh()` by hand after each self-mutation (and missed
edits made elsewhere).
- Added `Project::contactUpdated(uint64_t)`, emitted by `updateContact` + `setContactGroup`.
- `ProjectEventBus` now relays `contactUpdated` + `contactGroupsChanged` (+ `recycleBinChanged`).
- Contact Manager subscribes to those → refreshes from one place; **all six manual
  `refresh()` calls removed**. The view now mirrors project state reactively.

## 2. Recycle bin (project-scoped, persisted) — user request
Soft-delete subsystem on `Project`, persisted in the `.dlp` and surfaced in both the
sidebar panel and the Contact Manager, synced via the bus.
- `Project`: `m_recycled_contacts` + `recycleContact / restoreContact / purgeContact /
  emptyRecycleBin / recycledContacts()` + `recycleBinChanged()` signal. IDs stay stable
  across recycle/restore.
- Serialization: `recycled_contacts` array in the `.dlp` (write + read refactored to a
  shared `contactToJson` / `contactFromJson` lambda — no duplication; shared id counter).
- **Unified delete path**: `RecycleContactCommand` (undoable: redo recycle / undo restore)
  replaces hard delete everywhere — Contact Manager delete, `onRemoveContact`, and
  `onClearContacts` (now a macro of recycles). One delete behaviour app-wide.
- Contact Manager: a **Recycle Bin (n)** nav node; selecting it lists recycled contacts;
  context menu + command bar give Restore / Delete Forever / Empty Bin; non-applicable
  editing commands are disabled in the bin.
- Sidebar **Recycle Bin** panel (previously a dead "No deleted items." stub — a D-05
  violation) is now a live list with Restore / Delete Forever / Empty, synced via the bus.

## 3. Dead code removed
- `ContactListPanel.{h,cpp}` — built but never instantiated since the window replaced it
  (deleted + removed from CMake; fixed a stale comment in `WaterfallWindow.h`).
- `RemoveContactCommand` and `ClearContactsCommand` — unused after the unified recycle
  path (deleted from `LayerCommands.h`).
- Dead `GroupGroup` branch in `bucketKey` (Group section is built explicitly).

## Build
Full build + link clean (`forever` is a Qt keyword macro — a local named `forever`
broke the build; renamed to `purge`).

## Runtime verification (manual)
- Delete a contact → it leaves the list, appears in Recycle Bin (both surfaces) + persists
  after save/reopen. Restore → returns. Delete Forever / Empty → gone. Ctrl+Z after delete
  restores. Rename/favourite/group from elsewhere reflect live in an open manager.
