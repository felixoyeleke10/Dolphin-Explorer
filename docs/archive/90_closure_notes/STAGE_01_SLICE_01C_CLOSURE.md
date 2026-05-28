# Stage 01 Slice 01C Closure

## Scope

- active stage: `Stage 01`
- active slice: `01C`
- primary goal: enforce the artifact-store session contract so reader and index always come from the same store

## What Changed

- introduced a local artifact-store session helper in `ImportService.cpp`
- made sidescan load paths open a reader and pair it with an index from that same store
- removed the old mixed-store fallback behavior where a cache-backed layer index could be reused after falling back to the raw source reader
- kept the fallback-to-raw behavior, but now rebuilds the raw index when that fallback happens

## Files Touched

- `src/app/ImportService.cpp`

## Tests Or Validation

- direct `g++ -fsyntax-only` check for `src/app/ImportService.cpp` passed
- direct `g++ -c` compile invocation for `src/app/ImportService.cpp` returned success
- code path review confirmed:
  - primary store uses layer artifact-store path/format plus matching index
  - fallback raw source uses a raw reader plus raw-built index

## Gate Status

- gate items completed:
  - reader/index consistency for sidescan read paths
  - fallback-to-raw no longer reuses cache-derived offsets
- gate items still open:
  - metadata parity across cache-hit and raw-import paths
  - lightweight activation/loading correction
  - broader Stage 01 regression coverage

## Risks / Follow-Ups

- import/build path metadata parity is still handled in later slices, not fully closed here
- full project build/link verification is still blocked while `build_mingw\\DolphinExplorer.exe` is running

## What The Next Stage May Assume

- `loadSidescanWindow(...)` and `loadAllSidescanPings(...)` now operate on a coherent artifact-store session
- cache/raw fallback behavior no longer mixes offsets from one store with reads from another
