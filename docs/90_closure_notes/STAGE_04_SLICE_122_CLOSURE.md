# Stage 04 Slice 122 Closure

## Scope

- active stage: Stage 04 — Import Once Workflow Policy
- active slice: 122 — SSS/SBP project-reopen identity compatibility
- primary goal: reopen valid projects whose artifact indexes were saved with a
  parsed-store path instead of the layer's logical project source ID

## What Changed

- kept `DataLayer::source_id` as the authoritative, validated relationship to a
  `ProjectSource`
- treated a mismatched persisted `artifact_index.source_id` as recoverable legacy
  provenance instead of rejecting the entire project
- discarded any embedded offsets associated with that mismatch, then restored a
  complete index only from the layer's structurally validated durable DLPD store
- canonicalized loaded and rebuilt application-layer indexes to the logical
  `ProjectSource` ID
- serialized the authoritative layer source ID so newly saved manifests converge
  to one portable encoding
- added regressions for path-form legacy IDs, cross-source persisted IDs, and the
  asynchronous cache-rebuild/save/reopen producer path

## Files Touched

- `src/app/project/Project.Serialization.Layers.Read.cpp`
- `src/app/project/Project.Serialization.cpp`
- `src/app/services/ImportService.cpp`
- `tests/test_project_storage.cpp`
- `tests/test_import_classifier.cpp`

## Tests Or Validation

- inspected the reported schema-v11 manifest: four unique project sources, four
  unique layers, and every `layer.source_id` resolves correctly
- inspected all four referenced DLPD stores: valid v26 headers and valid index
  footers, with 12,602 / 11,168 / 7,894 / 9,868 sidescan entries
- ran the repaired loader against the exact reported manifest through a temporary
  read-only headless probe: `OPEN_OK sources=4 layers=4 entries=41532`; the probe
  bypassed open-time cleanup and was removed after use, so no survey file changed
- MSVC/Ninja compilation and all test targets: passed
- focused CTest (`ProjectStorage|ImportClassifier`): 2/2 passed
- full CTest: 23/23 passed in 146.25 seconds, including `PerfBaseline` and
  `GlSmoke`
- `git diff --check`: passed (existing line-ending conversion warnings only)
- final `DolphinExplorer.exe` relink: passed after the operator closed the running
  application; the initial `LNK1168` lock caused no source/build failure
- final `DolphinExplorer` + `tests/all` rebuild check: `ninja: no work to do`

## Gate Status

- gate items completed: compatibility recovery, producer correction, manifest
  normalization, regression coverage, final application build, full automated
  verification
- gate items still open: none for this slice

## Risks / Follow-Ups

- DLPD stores currently prove structural validity but do not embed a durable store
  identity/revision tying offsets to a specific survey source; that is separate
  provenance hardening and does not block this compatibility repair
- a manual GUI reopen remains useful after the final executable relink, but the
  exact external manifest and all four real DLPD indexes have already passed the
  repaired deserializer in a read-only headless run

## What The Next Stage May Assume

- released manifests may contain a path-form artifact-index source locator
- project loading safely canonicalizes that legacy encoding without trusting its
  persisted offsets
- rebuild and save paths no longer produce path-form project artifact identities
