# Stage 05 Slice 02 — Source Identity Hardening

## Goal
Fix the duplicated format-dispatch logic in `ImportService` so that `FormatRegistry`
is the single source of truth for "what formats are supported", and document the
`sha256` tombstone so no one mistakes it for a computed identity field.

## What Was Done

### `ImportService.cpp` — removed two duplicate statics
- Deleted `isSupportedFormat()`: hardcoded list (`xtf|jsf|segy|sgy|dlpd|dpcache`) that
  duplicated `FormatRegistry::isKnown()` and would silently miss any new format.
- Deleted `formatFromPath()`: local path→extension parser that duplicated
  `FormatRegistry::sniffFromPath()`.
- `reindexLayer()` now calls `FormatRegistry::instance().sniffFromPath(path)` for the
  format ID, then `checkImportPreflight(path, fmt)` for validation — matching the
  code path that `importFile()` already used.  `reindexLayer()` also gains the
  file-exists and file-size checks that `importFile()` had but `reindexLayer()` lacked.
- Error message for unsupported format now comes from `PreflightChecker` (which cites
  the format string it received) rather than a hardcoded sentence that omitted DLPD.
- Added explicit `#include "app/import/FormatRegistry.h"` (was previously only pulled
  in transitively through `PreflightChecker.h`).

### `Project.h` — documented dead `sha256` field
- Added `// D-02: reserved — not yet computed; always empty` comment.
- Field stays in the struct and roundtrips through JSON serialization for forward
  compatibility when SHA-256 is eventually added.
- No code was relying on `sha256` being populated; this change prevents future
  contributors from mistaking it for a valid identity fingerprint.

## What Was NOT Addressed (by design)
- **Workspace source registry**: whether `classifyImportAction()` should search other
  projects on the same filesystem.  D-02 says identity = path + size/mtime within a
  project; cross-project lookup requires a design decision (Stop-and-ask territory).
- **`io/ProbeDispatch.h::kSupportedFileFilter`**: kept as-is.  It serves the file-dialog
  layer (UI) and lives in `io`; `FormatRegistry` lives in `app`.  They serve different
  roles and cannot easily reference each other across the layer boundary.

## Build
Full 94-step build, zero errors, zero warnings.

## Files Changed
- `src/app/services/ImportService.cpp`
- `src/app/project/Project.h`
