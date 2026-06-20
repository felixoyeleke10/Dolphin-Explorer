# Stage 07 Slice 37 — True project file-rename + uniform contact undo

Two QC follow-ups the user chose after the band-aid review.

## A. True project rename (file move, not just display name)
Previously `onRenameProject` only did `setName` in memory. Now it renames the actual
project on disk.
- **`Project::renameOnDisk(new_path)`** — a filesystem *move* (instant), not a copy:
  `fs::rename` the `.dlp`, `fs::rename` the cache folder (`cacheRootForManifest`), prefix-swap
  each layer's `artifact_store_path`, then `save()`. Refuses to clobber an existing target and
  rolls back the manifest/cache moves on any failure.
- **`ProjectSessionController::renameProject(name)`** owns the operation (PSC owns project
  lifecycle): sets the display name; for a saved project sanitises the name to a filename,
  moves via `renameOnDisk`, updates the Recent list (old path → new path), clears the dirty
  flag (renameOnDisk saved), emits `recentProjectsChanged` + `windowTitleChanged`. Unsaved/temp
  projects get a name-only change. Name collision / move failure → warn + keep in-memory rename.
- `MainWindow::onRenameProject` is now just the dialog → `m_session_ctrl->renameProject`.
- Net: rename updates the title, the `.dlp` filename, its cache folder, and Recent — everywhere.

## B. Uniform undo for contact edits
Delete was undoable; rename/favourite/group-assign/paste mutated the project directly. Now all
go through the shared undo stack (window emits intent → MainWindow pushes commands; no
contacts→mainwindow dependency).
- **`UpdateContactCommand(before, after)`** (LayerCommands.h) — one command covers rename,
  favourite toggle, and group-assign (all are `Project::updateContact`); redo/undo re-apply the
  matching snapshot (id stable).
- ContactManagerWindow now emits `contactsEditRequested(before[], after[])` (batched → a
  multi-select favourite/assign is one undo step) and `contactsAddRequested(contacts[])` for
  paste; it no longer calls `updateContact`/`setContactGroup`/`addContact` directly.
- MainWindow wraps each in a `beginMacro/endMacro` of `UpdateContactCommand` / `AddContactCommand`.
- Cut+paste now recycles the originals via `removeContactRequested` (consistent soft-delete).

Ctrl+Z reverts rename, favourite, group-assign, paste, and delete uniformly (from the main
window, as before). Group *entity* create/rename/delete remain direct (structural; out of scope).

## Build
Full build + link clean.

## Runtime verification (manual)
- Rename a saved project → title, `.dlp` filename, cache folder, and Recent all update; reopen
  from Recent works. Rename to an existing name → warned, file not moved.
- Contact Manager: rename / favourite (multi-select) / add-to-group / paste, then Ctrl+Z (main
  window) → each reverts in one step.
