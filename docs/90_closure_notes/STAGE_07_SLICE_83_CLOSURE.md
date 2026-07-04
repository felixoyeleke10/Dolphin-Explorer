# Stage 07 · Slice 83 — Explorer visibility checkboxes for contacts + features

## Goal (user direction)
In the File Explorer tree only sensor lines had on/off checkboxes; contacts,
features — everything — should have them too.

## What changed

### Data model
- `core::Contact::visible` and `core::Feature::visible` (default true),
  serialized in the .dlp (`"visible"`); absent key on older projects reads as
  visible — no migration needed.

### Explorer tree (LineListPanel)
- Contact items (grouped + ungrouped, in both `buildContactsSection` and
  `refreshContacts`) and feature items (both build paths) are now
  `ItemIsUserCheckable` with checkState from the model flag.
- `onItemChanged` routes by item type: Layer → existing signal; Contact →
  new `contactVisibilityChanged(id, visible)`; Feature →
  `featureVisibilityChanged(id, visible)`.

### Undoable, single-authority mutation
- New `UpdateFeatureCommand` (mirrors `UpdateContactCommand`, uses
  `Project::updateFeature`).
- MainWindow.Shell connects the two signals → pushes
  `UpdateContactCommand` / `UpdateFeatureCommand` with only `visible`
  flipped — toggles are undoable like layer visibility, and every consumer
  updates through the normal bus flow.

### Consumers honour the flag
- 2D map: `paintContacts` / `paintFeatures` skip `!visible`.
- Waterfall overlay (`refreshContactOverlay`) and SBP overlay filter hidden
  contacts.
- Bus gaps closed: `contactUpdated` now also refreshes the explorer contact
  list and repaints the map (previously only add/remove did — an edit from
  the manager/editor left a stale tree row).

## Also in this slice
Finished the Mosaic Spotlight removal (user rejected the cursor circle):
panel section, signal, settings keys, MapView state/paint/input tracking all
gone; the Map tab keeps GENERAL (tooltips, hover highlight) and CAMERA
PROPERTIES.

## Round 2 — styling + lag QC (user feedback)
- **Item-view checkboxes rendered as raw black Fusion boxes** — tree/table
  check indicators are drawn by the VIEW, not by QCheckBox, and only
  `QCheckBox::indicator` was themed. Added indicator rules for all six
  item-view classes in AppStyleBase (mirroring the QCheckBox treatment:
  accent fill + white check, hover/disabled states); verified on screen.
  Also fixes the same latent issue in Contact Manager tables / export lists.
- **Toggle lag** — every `contactUpdated` rebuilt the ENTIRE Contacts tree
  section (delete + recreate all items, re-resolve labels, re-decorate tags)
  and re-derived the viewer overlays. Fixes:
  - New `refreshContactRow(id)` / `refreshFeatureRow(id)` update the one row
    in place (text, check state, tag decoration); full rebuild only as a
    fallback when the row is missing. Bus now routes `contactUpdated` /
    `featureUpdated` to the row-level refresh.
  - Viewer overlay resync (`setProjectContacts` on both viewers) is coalesced
    behind a 50 ms single-shot so toggle bursts collapse into one re-derivation
    (the waterfall's nearest-nav scan for unlinked contacts is O(rows) per
    contact — the expensive part).

## Verification
Full build green; `ctest -E PerfBaseline` → 16/16 passed.
