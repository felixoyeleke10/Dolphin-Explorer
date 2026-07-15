# Stage 08 Slice 111 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-111 — contact-export consistency and write integrity
- primary goal: remove duplicate CSV implementations and prevent export success from being reported before bytes are durably accepted

## What Changed

- MainWindow/Export Manager CSV export now delegates to the same `ContactReport::writeCsv` implementation used by contact dialogs.
- Established one canonical machine-readable CSV schema with coordinates, geometry, classification, confidence, line, and notes; quoting covers commas, quotes, and embedded newlines.
- CSV output now uses `QSaveFile`, checks the exact byte count, and commits atomically instead of ignoring `QFile::write` results.
- CSV extensions are normalized at the export-controller boundary.
- PDF output now paints into a transactional `QSaveFile` device and commits only after the writer closes without a device error.
- `ZipWriter` now flushes and closes before reporting success, then atomically replaces the destination; delayed stream errors or a failed publish cannot corrupt an existing DOCX.

## Files Touched

- `src/ui/features/contacts/ContactReport.{h,cpp}`
- `src/ui/mainwindow/coordinators/ExportController.cpp`
- `src/util/ZipWriter.cpp`
- `tests/test_contact_report.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- New `ContactReport` test: 12 checks passed, covering UTF-8 BOM, canonical columns, precision, CSV escaping, embedded newlines, and an unwritable destination.
- `ZipWriter`: 15 checks passed, including structural validity and replacement of an existing archive on Windows/POSIX.

## Gate Status

- gate items completed: all contact CSV entry points share one writer/schema; CSV, PDF, and DOCX outputs use transactional or atomic publication and do not claim success before output completion.
- gate items still open: interactive file-dialog flows remain manual UI coverage.

## Risks / Follow-Ups

- The canonical CSV intentionally differs from the formatted PDF/DOCX table: CSV preserves raw data fields for downstream analysis, while reports preserve human-readable sensor/source context.

## What The Next Stage May Assume

- Contact CSV export has one atomic implementation regardless of which UI entry point invoked it.
