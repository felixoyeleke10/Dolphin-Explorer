# Stage 07 Slice 38 — Systems QC: loophole fixes, unified export, group undo

## Loophole fixes (from the systems QC)
- **DOCX corruption on control chars** — `ContactReport`'s XML escaper now drops characters
  invalid in XML 1.0 (control chars other than tab/LF/CR); previously a contact label/notes
  with one (or `\0`) made the `.docx` unopenable in Word.
- **Dangling group id on recycled contacts** — `Project::removeContactGroup` now also clears
  `group_id` on `m_recycled_contacts`, so a restored contact never points at a deleted group.
- **PDF false success** — `ContactReport::writePdf` now returns whether the file actually
  exists and is non-empty (an unwritable path no longer reports success).

Verified-and-sound (no change needed): paste-undo (`AddContactCommand` captures the assigned
id), and `cacheRootForManifest` is `<dir>/data` so an in-place rename's cache-move branch is a
correct no-op (no catastrophic directory move).

## Unified export scope
The Contact Manager had two actions with different scope (Export = all contacts as CSV via
MainWindow; Report = visible set as PDF/Word). Merged into **one Export action** offering
**CSV / PDF / Word**, all on the **visible set** (current folder + search).
- Added `ContactReport::writeCsv` (UTF-8 BOM, RFC-4180 quoting) so all three formats share the
  one row model.
- Removed the now-dead `exportContactsRequested` signal + its MainWindow→`onExportCsv` wiring.
  The app-level Export menu/toolbar (all-contacts CSV) is untouched.

## Group create/rename/delete on the undo stack
Group-assign was already undoable (`UpdateContactCommand`); now the group *entity* ops are too,
via the same window-emits-intent → MainWindow-pushes-command pattern (no contacts→mainwindow dep):
- `AddContactGroupCommand` (exposes the new id), `RenameContactGroupCommand`,
  `RemoveContactGroupCommand` (snapshots name + member ids before redo; undo re-creates the
  group and re-assigns members; keeps the fresh id for redo).
- New window signals `groupAddRequested / groupRenameRequested / groupRemoveRequested /
  groupAddAndAssignRequested`; "New Group… + assign" is one undo macro.
- The window no longer calls `addContactGroup`/`renameContactGroup`/`removeContactGroup` directly.

Ctrl+Z (main window) now reverts every Contact Manager mutation: delete, rename, favourite,
group-assign, paste, and group create/rename/delete.

## Build
Full build + link clean.

## Runtime verification (manual)
- Export → CSV/PDF/Word each save the **currently listed** contacts.
- Create / rename / delete a group, then Ctrl+Z → reverts (delete-group undo restores the group
  and its members).
