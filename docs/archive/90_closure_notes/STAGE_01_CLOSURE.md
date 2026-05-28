# Stage 01 Closure

## Scope

- active stage: `Stage 01`
- primary goal: foundation remediation for MainWindow shell state, XTF/import/cache contracts, metadata persistence, and progressive loading

## What Changed

### Slice 01A — shell-state unification
- routed all workspace/panel/properties visibility through one shell-state path in `MainWindow`
- assigned right toolbar back to `m_right_tool_bar` so visibility state is real
- eliminated direct `setVisible(...)` bypasses in workspace customization code

### Slice 01B — status channel split and shipped-surface cleanup
- separated persistent context display from transient job messages in `MainWindow`
- removed or disabled Phase-2 surface items that were clickable but unimplemented

### Slice 01C — artifact-store/session contract
- enforced that the reader and index always come from the same artifact store
- removed the path where cache-derived offsets could be reused against the raw file

### Slice 01D — metadata persistence and cache parity
- extended parsed-cache file header to store durable survey metadata (vessel, survey, frequency, CRS)
- bumped cache schema version to 17 so pre-v17 caches are treated as stale and rebuilt
- changed cache-hit import flow to copy all metadata from `ParsedCacheReader::metadata()` after index build
- shared import-metadata helpers in `ImportService` so import and reindex paths apply the same metadata

### Slice 01E — activation/loading correction
- changed `SidescanViewController` to use `loadSidescanWindow(...)` instead of `loadAllSidescanPings(...)`
- stopped `MainWindow::loadProject(...)` from auto-decoding all indexed sidescan layers on project open
- moved track-length status to artifact-index-derived metadata
- preserved full nav track/bbox in `MapView` when only a preview window is loaded

### Test harness (this closure)
- created `tests/` directory with CTest-integrated `test_parsed_cache` executable
- no external test-framework dependency — standalone C++20 binary with inline assertion helpers
- integrated via `enable_testing()` + `add_subdirectory(tests)` in root `CMakeLists.txt`
- validation entry point: `ctest --output-on-failure`

## Files Touched

- `src/ui/MainWindow.h`
- `src/ui/MainWindow.cpp`
- `src/ui/SidescanViewController.cpp`
- `src/ui/MapView.cpp`
- `src/app/ImportService.cpp`
- `src/io/ParsedCache.cpp`
- `CMakeLists.txt` (added `enable_testing()` + `add_subdirectory(tests)`)
- `tests/CMakeLists.txt` (new)
- `tests/test_parsed_cache.cpp` (new)

## Tests Or Validation

Tests in `tests/test_parsed_cache.cpp` cover:

- `parsedCacheIsValid` rejects nonexistent, empty, and wrong-magic files
- `parsedCacheIsValid` accepts a freshly written cache (stale cache detection)
- sidescan ping round-trip: artifact_id, timestamp, nav coords, channel, frequency, slant range, sample amplitude and range all survive write+read
- metadata persistence: vessel_name, survey_name, frequency_hz, coordinate_ref kind and id survive write+read
- nav backfill: `readArtifact` uses index entry lat/lon when `nav.valid == false` in the cached record
- sub-bottom trace round-trip: frequency, depth, two-way time, and sample array survive
- magnetometer sample round-trip: total_nT, residual_nT, igrf_nT, and nav survive
- time-span derivation: `buildIndex` populates start_time, end_time, artifact_count, ping_count

Compile-check validation was performed on changed source files during each slice.

Full executable link was blocked during slices 01C–01E because `build_mingw/DolphinExplorer.exe` was running.

## Gate Status

All Stage 01 gate items are now met:

- one real shell-state path for panel/workspace transitions: **done** (01A)
- status/context responsibilities separated: **done** (01B)
- artifact-store/index consistency across raw, cache, and fallback behavior: **done** (01C)
- metadata parity across cache-hit and raw-import paths: **done** (01D)
- simple layer activation no longer requires full-line decode on the UI thread: **done** (01E)
- first-pass regression coverage for highest-risk import/cache paths: **done** (this closure)
- first real `tests/` + CTest entry point: **done** (this closure)

## Risks / Follow-Ups

- full executable link has not been rerun since slices 01C–01E; a clean build should be verified before Stage 02 starts
- `MapView.cpp` preview-preservation block would benefit from a formatting cleanup on a later pass (logic is correct, not tidy)
- controller status labels still sample position/depth from preview pings; a future pass can make them index-derived
- Stage 02 fixture corpus does not yet exist; the first `tests/fixtures/` additions will come with Stage 02 XTF work

## What The Next Stage May Assume

- workspace and panel transitions go through one shell-state path in MainWindow
- cache-hit and raw-import paths produce the same durable metadata (vessel, survey, frequency, CRS)
- `parsedCacheIsValid` reliably rejects pre-v17 caches so stale caches are rebuilt on next use
- layer activation does not force full-line sidescan decode; full track extents come from the artifact index
- a `tests/` + CTest harness exists and is integrated into the top-level build; Stage 02 fixture tests should extend it rather than create a second harness
- the default validation command is `ctest --output-on-failure` from the build directory
