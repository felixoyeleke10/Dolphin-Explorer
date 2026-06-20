# Stage 07 Slice 39 — Export Manager (project-wide export hub)

User request: an Export Manager. Chosen: **project-wide hub**, as a **standalone window**.

## New module `ui/features/export/` (lib `dolphin-ui-export`)
`ExportManagerWindow` — pure UI emitting an intent signal per export; MainWindow performs the
work. New CMake lib (Qt6::Widgets only), linked into `dolphin-ui-mainwindow` + the `dolphin-ui`
aggregate.

**Designed like the Contact Manager** (`QMainWindow`, 3-pane, styled): a left **nav tree** of
export targets, a centre **config pane** (title + description + segmented format buttons + a
primary Export button), a right **preview/summary pane** (target icon + title + detail), and a
status bar. (Replaced the first plain card-list version the user flagged as too low-effort.)

Targets (driven by a small data table):
- **Contacts** → CSV / PDF / Word.
- **Screenshot** → PNG.
- **Coming soon** (non-selectable, greyed — D-5): GeoTIFF mosaic, KMZ, Navigation track.

Signals: `exportContactsCsvRequested / …PdfRequested / …WordRequested / exportScreenshotRequested`.
Selecting a target rebuilds the format buttons + preview; Export emits the signal for the
chosen target + format.

## MainWindow wiring (does the real work — owns project + handlers)
- `onExportManagerOpen()` — lazy window (WA_DeleteOnClose), connects the signals:
  - CSV → existing `onExportCsv()` (all contacts, full column set + its own save dialog).
  - PDF/Word → new `exportContactsReport(bool docx)` — all project contacts → `ContactReport`
    with a save dialog; reports success/failure.
  - PNG → existing `onExportScreenshot()`.
- Entry points: **File ▸ Export ▸ Export Manager…** and the top-toolbar **Export ▾** dropdown.

## Design notes
- Reuses the real export paths; the stub exporters (GeoTIFF/KMZ/Nav/layers/survey-PDF) are
  shown disabled rather than wired to "not yet available" handlers — honest, D-05-compliant.
- Scope split: hub exports the **whole project's** contacts; the Contact Manager's own Export
  still exports the **visible/filtered** set. Two intentional scopes (whole-project vs in-view).

## Build
New lib + reconfigure; full build + link clean.

## Runtime verification (manual)
- File ▸ Export ▸ Export Manager… (or toolbar Export ▾) opens the hub.
- Contacts CSV/PDF/Word and Screenshot each save; disabled cards are greyed with "coming soon".
