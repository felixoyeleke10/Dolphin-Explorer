# Stage 04 Closure Note

**Stage:** 04 — Import-Once Workflow Policy  
**Closed:** 2026-06-06  
**Status:** Complete

---

## What this stage delivered

### Import decision classifier

`app/import/ImportClassifier.h / .cpp` — `classifyImportAction(path, project)`.

Pure app-layer function (no UI dependency) that inspects the current project and returns one
of three decisions before any side effect runs:

| Kind | Condition |
|------|-----------|
| `ImportNew` | Source path not in project |
| `ReuseExisting` | Source known + valid DLPD cache present |
| `RebuildExisting` | Source known but cache missing or stale |

For `ReuseExisting` and `RebuildExisting`, `existing_layer_id` and `existing_source_id` are
populated so `ImportJobManager` can route directly to the right layer without re-scanning.

When multiple layers share a source (e.g. a dual-frequency split), the classifier picks
the "best" layer: `pipeline_applied = true` beats raw; `index_built = true` beats placeholder.

---

### Wizard per-file status badges

`ImportReviewWizard::onProbeFinished` runs the classifier after each background file probe
completes and caches the result in `FileEntry::classify_kind`.

`updateFileRow` shows:
- **"Already indexed"** (green) — `ReuseExisting`
- **"Rebuild needed"** (amber) — `RebuildExisting`
- **"Projected / Geographic / Needs CRS"** — `ImportNew` (existing behaviour unchanged)

Users see the decision before clicking Import.

---

### Managed project lookup

`ensureProjectForImport` scans the app-managed project directory (`AppLocalDataLocation/
projects/`) for any `.dlp` manifest containing the imported source paths before creating a
new `Session_*` temp project.

If a match is found:
> "A project already contains this data: [name]. Open it?"

Accepting opens the existing project.  The import lambdas take `ImportDialogResult` by
value and call `reclassify()` after `ensureProjectForImport` returns, so wizard-computed
`ImportNew` entries are upgraded to `ReuseExisting` / `RebuildExisting` for the now-open
project.

---

### Batch deduplication

`ImportJobManager::importBatch` deduplicates repeated identical paths within one batch
before any side effect.  `QFileInfo::canonicalFilePath()` normalises symlinks and
case on Windows; the raw path is the fallback for files not yet on disk.  Duplicates
are logged as `Suppressed` events in `ImportLog`.

---

### Regression tests

`tests/test_import_classifier.cpp` — six tests:

1. Null project → `ImportNew`
2. Unknown source path → `ImportNew`
3. Valid DLPD present → `ReuseExisting` with correct `existing_layer_id`
4. DLPD missing or stale → `RebuildExisting`
5. Multiple layers sharing source → `pipeline_applied = true` layer wins
6. Duplicate paths in one `importBatch` call → second suppressed, one `jobCompleted`

---

## What was tested

- `test_import_classifier` — all six correctness assertions pass
- Manual smoke: drag-drop a file already in an open project → "Already indexed" badge appears
- Manual smoke: import same file twice in one drag-drop → no duplicate layer created
- Code review confirms `ReuseExisting` path skips `ImportService::importFile` entirely

---

## What Stage 04 did not implement

Per the plan, the following are explicitly out of scope:

- **Cross-project registry service**: The managed-directory scan in `findManagedProjectForPaths`
  is the closest approximation, but it opens every `.dlp` file in the directory.  A proper
  registry (indexed lookup of source paths across all projects without opening each manifest)
  is a future `D-01` expansion.

- **Network or multi-user registry**: Out of scope per D-01 (`Locked`).

- **"Source missing" detection before task dispatch**: The import path still does not check
  whether the raw source file exists before queueing a rebuild.  `ImportService::reindexLayer`
  will fail gracefully, but there is no upfront warning to the user.

---

## Decision log decisions satisfied

| Decision | Outcome |
|----------|---------|
| D-01 local workspace scope | Met — scan limited to `AppLocalDataLocation/projects/` |
| D-02 source identity = path + fingerprint | Classifier uses `findSourceByPath` (path match) |
| D-03 existing project lookup before new project | Met — `findManagedProjectForPaths` |
| D-11 batch dedup | Met — `ImportJobManager::importBatch` deduplicates |
| D-13 regression coverage | Met — `test_import_classifier` covers all classifier branches |
