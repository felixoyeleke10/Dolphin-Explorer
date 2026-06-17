# Stage 07 Slice 19 — Background Tasks dialog: pipeline/stage view

User feedback: the "Background Tasks" dialog showed each line as a **DLP file card**
(format badge + size + "✔ N artifacts cached"), which is a *file/artifact report*, not
a *progress* view — every row showed a green ✔ while the header said "Loading into
map…", so the rows contradicted the status; "artifacts" is jargon; the bar was
indeterminate. User chose the **pipeline / stage view** (asked via mockups).

## What changed (ExecutionProgressDialog)
Reworked the content area from per-line cards to a **stage stepper + current-activity
line**, while keeping the proven row/state machinery underneath.

- **Stage stepper** (`buildStageChips` / `updateStages`): a horizontal
  `● Reading → ○ Building map` row. Each chip shows pending `○` (muted) / active `●`
  (accent, bold) / done `✓` (success). Stages adapt to the operation kind, inferred
  from the first job's format tag:
  - import (file formats) → **Reading → Building map**
  - correction/processing (`COR`/`SBP`/`RUN`) → single **Processing** stage
- **Current-activity line** (reuses `m_sub_lbl`): "Reading <line>…",
  "Building map — X of Y", or "N line(s) complete", plus "· K failed" if any.
- **Title**: "Importing N line(s)" / "Processing N line(s)" / "All Done".
- **Overall bar**: unchanged math (still driven by the row states).

### Low-churn / low-risk approach
The per-line `FileRow` state + `addJob/updateJob/finishJob/failJob` are untouched —
the cards are still built into the list, but the scroll area is **hidden**
(`m_scroll->hide()`), so progress/state tracking is exactly as before and only the
presentation changed. `updateStages()` is driven from that existing state
(`m_rows` + `m_pending_map_loads` + a new `m_map_total` counter incremented in
`onMapLoadPending`). Stage chips are rebuilt per batch (reset in the new-batch path).

`checkAllDone` no longer hard-sets "Loading into map…"; `updateStages` owns the
sub-line so the stepper and text stay consistent.

## Build / tests
Compiles clean (ImportProgressDialog object rebuilt) and the exe relinked with this +
the Slice 18 panel fix. No tests cover this dialog (pure UI); **needs the user's
runtime check** — import a few lines and confirm the stepper advances
Reading → Building map with a live "Building map — X of Y" line and honest %.

## Correction — stepper alone was too sparse; added per-line rows back
First cut hid the per-line list and showed only the stepper + one "current line"
text. User feedback: "it's only showing one line for all lines." Fixed by restoring
the per-line list (un-hidden) as **clean status rows** under the stepper — not the old
DLP boxes:
- Row = status icon (`●` reading / `✓` done / `✕` failed, colored via `applyCardState`)
  + line name + size + a thin 4px progress bar while active; on finish the bar is
  replaced by "N records · CRS · kHz" (was "N artifacts"); on failure the error text.
- The old DLP format badge is gone (replaced by the status icon); the redundant
  ✔/✕ text prefixes are gone (the icon carries status).
- The stepper sub-line is now an **aggregate** summary ("Reading… X of Y lines" /
  "Building map — X of Y" / "N complete · K failed") since per-line detail is in the
  rows. So you see the overview (stepper) AND every line's live status (rows).

Built clean (exe relink blocked only by LNK1168 while the app runs).

## Row CSS fix — true single-line rows
The first row layout was a 2-row `QVBoxLayout` (text row + a separate progress-bar
row) inside a bordered `fileCard` box, so rows stacked vertically / showed multiple
lines and long content overflowed (looked like it needed a horizontal scroller).
Reworked to a genuine single line:
- `buildCard` is now one `QHBoxLayout`: `[status icon] [name (stretch, elided 260px)]
  [status/result (right-aligned)]` — no nested vertical, no per-row bar.
- Per-row progress is shown as text ("Reading X%") in the result label; overall
  progress stays in the top bar. `FileRow` gained `int percent` (drives the overall
  bar without a per-row `QProgressBar`).
- Result kept short — "N records · size" on done (CRS/freq dropped; they live in the
  inspector), the error elided + full text on hover when failed.
- QSS: `#fileCard` is now a borderless list row (transparent, bottom-border separator,
  hover highlight) instead of a chunky box; list spacing set to 0. Horizontal scroll
  stays off, and the name elide keeps rows within the fixed 520px width.

## Follow-ups (optional)
- The hidden cards still build (minor wasted widgets); a later cleanup could make
  `FileRow` state-only and drop the card/scroll code entirely.
- `m_has_map_phase` is currently set but unused (the done-when-`pending==0` rule
  covers the no-map case); keep or remove in that cleanup.
- If desired, add real Coverage/Mosaic sub-stages once the map build emits per-phase
  signals (today it's one "Building map" step).
