# Stage 08 — Slice 95 Closure: Schema version constant + forward-compat guard

## Goal
S-95 of `docs/10_plan/STAGE_08_SYSTEMIZATION_HARDENING.md`: replace the
magic-numbered manifest version with a named constant and refuse manifests
written by a newer app version instead of silently misparsing them.

## Changes
- `Project.h` — `static constexpr int kSchemaVersion = 10;` with the bump rule
  documented (any serialization change bumps it + adds a read-side migration
  branch). New `m_load_error` member: fromJson's user-presentable failure
  reason. `Project::open` gains an optional `std::string* error` out-param
  (default nullptr — all existing callers unaffected).
- `Project.Serialization.cpp` — writer stamps `kSchemaVersion`.
- `Project.Serialization.Read.cpp` — `version > kSchemaVersion` → refuse with
  "saved by a newer version of Dolphin Explorer (manifest vN; this build reads
  up to vM)".
- `ProjectSessionController.cpp` — open failure now surfaces the specific
  reason (dialog + DiagnosticsHub problem) when the model provides one.

## Tests
`test_project_storage.cpp` `testSchemaVersionGuard`:
- future-version manifest (kSchemaVersion+1) → open returns nullptr, error
  mentions "newer version";
- legacy v1 manifest → still opens (migration, not rejection);
- freshly saved manifest carries `version == kSchemaVersion` (parsed, not
  string-matched).

145 checks pass in ProjectStorage (was 120); full suite 16/16 green.
