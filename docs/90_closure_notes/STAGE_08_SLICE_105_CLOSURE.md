# Stage 08 Slice 105 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-105 — parsed-cache reuse integrity
- primary goal: prevent header-only, truncated, or footer-corrupt DLPDs from being classified as reusable, and prevent failed empty writes from publishing a durable artifact

## What Changed

- `parsedCacheIsValid` now requires a compatible header plus a readable, non-empty compact index footer; it remains a bounded footer read and never scans payloads.
- Footer entries are rejected when their record offsets/lengths point before the data region, overlap a prior record, enter the footer, or exceed the file.
- Both DLPD write paths reject empty, undecodable, unsupported-only, and mixed unsupported candidates instead of silently publishing an incomplete durable result.
- Candidate files atomically replace an existing destination on Windows as well as POSIX; normal rebuilds no longer fail merely because a durable cache already exists.
- Index-footer write failures now abort publication instead of being ignored.
- Footerless legacy stores append their acceleration footer defensively: stream/close failure rolls a partial append back, and cached file size changes only after success.
- Project-open comments and recovery semantics now describe the stronger header/footer/index contract: footerless legacy stores are rebuilt in the background, while corrupt/truncated stores cannot reuse stale manifest offsets.
- Import-classifier fixtures now use a real one-record DLPD rather than treating a failed header-only write as valid test data.

## Files Touched

- `src/io/cache/ParsedCache.{h,cpp}`
- `src/io/cache/ParsedCache_p.h`
- `src/io/cache/ParsedCache.Read.cpp`
- `src/app/project/Project.Serialization.Read.cpp`
- `tests/test_parsed_cache.cpp`
- `tests/test_import_classifier.cpp`

## Tests Or Validation

- `ParsedCache`: 108 checks passed, including header-only/truncated rejection, empty/mixed/undecodable-write cleanup, existing-cache preservation and replacement, and footerless-store repair.
- `ImportClassifier`: 31 checks passed, including incomplete DLPD → `RebuildExisting` and real one-record DLPD → `ReuseExisting`.
- `ProjectStorage`: passed after the stricter cache-validity semantics.
- Relevant MSVC/Ninja targets rebuilt successfully.

## Gate Status

- gate items completed: durable parsed artifacts can no longer be reused on header/version evidence alone; failed or partial writes do not replace or leave misleading DLPDs; valid rebuilds can replace their destination on Windows.
- gate items still open: JSF false-positive probing and other repository-wide QC findings remain separate slices.

## Risks / Follow-Ups

- A compatible pre-footer legacy DLPD is intentionally not considered immediately reusable. Project open retains its existing background full-scan path, which can validate the records and append a modern footer.
- Footer validation is structural; individual payload decoding remains protected by `readArtifact` and parser-specific tests.

## What The Next Stage May Assume

- `parsedCacheIsValid(path)` means the cache has a structurally usable, non-empty footer/index suitable for fast reuse—not merely a recognizable header.
- A `false` return from either DLPD writer leaves the requested destination unpublished or preserves the previous durable file.
