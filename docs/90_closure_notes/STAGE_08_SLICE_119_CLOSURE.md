# Stage 08 Slice 119 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-119 — strict JSON and project-manifest validation
- primary goal: make malformed persistence input fail deterministically and prevent lossy identities or unsafe partial artifact indexes from entering a loaded project

## What Changed

- Replaced the permissive JSON reader with a complete-document parser that validates separators, literals, number grammar, escapes, Unicode surrogate pairs, termination, and trailing input. Malformed or out-of-range input returns a null value instead of leaking a conversion exception.
- JSON serialization now escapes every control character and emits `null` for non-finite numbers. Integer/float accessors saturate safely rather than invoking an out-of-range numeric cast.
- Project loading now requires a non-empty manifest name, validates every expected collection's container type, and rejects non-object collection members.
- Source and layer IDs must be non-empty and unique, and every layer must reference a declared source. Contact and feature IDs must be positive, integral, unique, and exactly representable by the JSON double model; active and recycled contacts share one identity set. Duplicate persisted group IDs are also rejected.
- Persisted artifact indexes must belong to the layer's source and contain only valid artifact types and exact in-range integral fields, including a non-zero byte length. One malformed entry invalidates the whole persisted seek table so the durable store can restore it or the layer can rebuild it; a partially accepted table is never used.
- Bounded persisted model/display fields are clamped or sanitized during load instead of wrapping into invalid enum, dimension, size, timestamp, opacity, blend, channel, or sample values.

## Files Touched

- `src/util/Json.{h,cpp}`
- `src/app/project/Project.Serialization.Read.cpp`
- `src/app/project/Project.Serialization.Layers.Read.cpp`
- `src/app/project/Project.Serialization.Entities.cpp`
- `tests/test_project_storage.cpp`

## Tests Or Validation

- Extended `ProjectStorage` coverage with malformed/truncated JSON, trailing garbage, invalid number grammar, missing/wrong-shaped containers, missing/duplicate identities and references, lossy/fractional entity IDs, duplicate groups, artifact-index source mismatch, all-or-nothing bad-index recovery, legacy `lines` compatibility, numeric sanitization, non-finite serialization, and control-character round trips.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including the new manifest, graph, and style regressions.

## Gate Status

- gate items completed: JSON input is grammar-checked as a complete document; project identity/reference invariants are enforced at the persistence boundary; persisted artifact offsets cannot be partially trusted after one invalid entry.
- gate items still open: aggregate build/test verification and manual inspection of user-presented load-error wording remain part of the final QC gate.

## Risks / Follow-Ups

- `JsonValue` intentionally retains a double-only number model. Any new persisted integer identity or file-offset field must use an exact-range validator at its owning schema boundary.
- Rejecting a manifest seek table can schedule an index rebuild when its durable store cannot supply a valid footer. This is a deliberate recovery cost in preference to seeking with untrusted offsets.

## What The Next Stage May Assume

- `parseJson` returns a value only for one complete, finite, grammatically valid JSON document.
- A successfully loaded project has unique source/layer/entity identities, valid layer-to-source references, correctly shaped collections, and either a wholly valid persisted artifact index or no trusted persisted entries.
