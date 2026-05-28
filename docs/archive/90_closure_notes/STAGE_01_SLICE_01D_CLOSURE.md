# Stage 01 Slice 01D Closure

## Scope

- active stage: `Stage 01`
- active slice: `01D metadata persistence and cache parity`
- primary goal: make cache-hit imports return the same durable survey metadata contract as raw parses

## What Changed

- added shared import-metadata helpers in `ImportService` so import and reindex paths apply the same metadata and source-fingerprint updates
- changed cache-hit import flow to copy survey/vessel/time/frequency/CRS metadata from `ParsedCacheReader::metadata()` after the cache index is rebuilt
- extended the parsed-cache file header to store durable survey metadata instead of only CRS data
- bumped the parsed-cache schema version so older metadata-poor caches are treated as stale and rebuilt from raw sources

## Files Touched

- `src/app/ImportService.cpp`
- `src/io/ParsedCache.cpp`

## Tests Or Validation

- direct compile check passed for `src/app/ImportService.cpp`
- direct compile check passed for `src/io/ParsedCache.cpp`
- full executable link was not rerun because `build_mingw/DolphinExplorer.exe` is still running and would block the final link step

## Gate Status

- gate items completed:
  - cache-hit imports now return the same metadata fields as raw imports
  - import and reindex layer updates use one metadata application path
  - parsed caches now persist durable survey metadata alongside CRS
- gate items still open:
  - `01E` activation/loading correction plus regression tests

## Risks / Follow-Ups

- existing `v16` parsed caches will be rebuilt on next use because the header schema changed
- metadata parity is now stronger, but full regression coverage still belongs to the remaining Stage 01 test work

## What The Next Stage May Assume

- cache-hit and raw-import paths now produce the same layer metadata fields for survey name, vessel name, time span, frequency, and source CRS
- import and reindex code no longer need separate metadata-assignment logic for linked layers
- parsed-cache metadata is durable enough for follow-on work in `01E` without depending on raw-file reopen just to restore layer metadata
