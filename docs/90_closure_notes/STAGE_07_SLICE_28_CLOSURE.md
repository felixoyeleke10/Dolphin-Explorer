# Stage 07 Slice 28 — Detect-then-confirm import (SonarWiz-style)

## Why
The old flow asked "What are you importing?" in a blind modal **before** the app looked
at any file (`ImportSetupDialog`), and every modality-specific menu command
("Sub-Bottom → SEG-Y", "Sidescan → XTF", …) funnelled through that same generic dialog
which defaulted to Sidescan and ignored the menu choice. That's backwards from how pro
tools (SonarWiz, Hypack, EdgeTech, QPS) work: they **probe the file and show what's in
it**, then you confirm which data types to bring in.

## What changed — the wizard detects, the user confirms
The probe already reports detected families (`ProbeResult::has_sidescan/subbottom/
magnetometer/multibeam`); the wizard now surfaces them:

- **Per-file modality checkboxes.** After each file is probed, its row shows a checkbox
  for every modality the file actually contains. `FileEntry::module_filter` is derived
  from the checkboxes; toggling re-classifies that row live. A file with a single family
  shows it locked-on (nothing to choose).
- **Seeding.** If a modality-specific menu command was used, only that family is
  pre-checked; otherwise everything detected is pre-checked.
- **No more "wrong sensor type" rejection.** A file is only skipped when nothing is
  selected ("Select a type") or it has no recognised sonar family ("No sonar data").
- **`onAccept`** skips files with an empty selection; the chosen families flow through
  `module_filter` to the (modality-aware, Slice 27) classifier.

## Flow wiring
- `ImportSetupDialog` (the blind upfront picker) is **removed** — files, CMake entry,
  and the `onImportFile` call site. `MainWindow::importFilesWithPreset(preset)` opens the
  wizard directly.
- Modality-specific **menu commands now pre-check their family** (`importAs({…})`) instead
  of re-asking; "Browse All Formats", the toolbar Import button, and drag-drop open with
  everything detected pre-checked.

## Result
Import is now detect-then-confirm: select files → the wizard probes and shows the
modalities each file holds → you confirm/adjust per file → import. One mixed file can
yield SSS + SBP layers in a single pass, and a modality-specific command no longer
double-asks. Combined with Slice 27 (modality-aware reuse), importing SBP after SSS from
the same file now creates the SBP layer correctly.

## Build / tests
All libraries compile; `ImportClassifier` 24/24. Exe relink needs the app closed. UI is
manual-verify: open Import, add a mixed XTF, confirm per-file modality checkboxes appear
and drive what's imported.
