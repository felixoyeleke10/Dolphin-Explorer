# Stage 08 Slice 113 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-113 — shared atomic file publication
- primary goal: give durable binary/text outputs one Windows/POSIX publication contract so a failed rewrite cannot corrupt or silently replace the previous asset

## What Changed

- Added `util::siblingTempPath` and `util::replaceFileAtomically` as the shared same-filesystem candidate/publication primitive. Windows uses one replace operation with write-through semantics; POSIX uses one filesystem rename.
- Migrated parsed-cache, ZIP, and GeoTIFF publication onto the shared helper instead of maintaining separate replacement behavior in each writer.
- GeoTIFF writers now create a sibling candidate, validate georeferencing/band/pixel writes and close-time GDAL status, then publish only a complete file. Rejected candidates are removed and an existing destination remains intact.
- Metadata-table CSV export now normalizes the `.csv` extension, writes through `QSaveFile`, checks stream/device status, and reports a commit failure instead of treating an opened file as a successful export.

## Files Touched

- `src/util/AtomicFile.{h,cpp}`
- `src/util/CMakeLists.txt`
- `src/util/ZipWriter.cpp`
- `src/io/cache/ParsedCache.Read.cpp`
- `src/io/raster/RasterWriter.cpp`
- `src/ui/features/metadata/MetadataExportUtils.cpp`
- `tests/test_parsed_cache.cpp`
- `tests/test_raster_io.cpp`
- `tests/test_zip_writer.cpp`

## Tests Or Validation

- Regression coverage exercises replacement of an existing ZIP, DLPD, and GeoTIFF and preservation of an existing destination after a rejected cache/raster candidate.
- Focused `RasterIO` verification passed after the publication and CRS changes.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `PerfBaseline` and `GlSmoke`.

## Gate Status

- gate items completed: durable cache/archive/raster writers share one cross-platform atomic replacement primitive; metadata CSV uses Qt's transactional save path; incomplete candidates do not become the destination.
- gate items still open: future output formats must adopt `AtomicFile` or `QSaveFile` rather than reintroducing direct truncate-in-place writes.

## Risks / Follow-Ups

- Candidates intentionally live beside the destination so publication does not cross filesystems. A destination directory that cannot host the candidate fails without modifying the old file.
- Publication protects the destination from partial content; it does not make several related output files one multi-file transaction.

## What The Next Stage May Assume

- Code that needs to publish a completed standard-library file can use `util::siblingTempPath` plus `util::replaceFileAtomically` on Windows and POSIX.
- A failed ZIP, parsed-cache, GeoTIFF, or metadata-CSV rewrite does not report success after merely opening or partially writing the destination.
