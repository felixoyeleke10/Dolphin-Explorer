# Stage 07 · Slice 92 — Multi-line import data loss: root cause + self-heal

## Report
"Imported three lines into SBP, only one works — something is 100% wrong."
Diagnosis on the SBP-Only project confirmed: of three imported lines, two
were dead placeholder layers (state=Placeholder, no artifact index, no
store path) persisted in the .dlp since import day.

## Root cause — save-time orphan purge races concurrent imports
`Project::save()` called `purgeOrphanedCaches()`, which deletes every .dlpd
in `data/` not referenced by a layer. But every import completion SAVES the
project — and with N files importing concurrently (job cap = cores), a
sibling import's freshly written cache is not referenced until ITS OWN
completion commits the store path. So the first line to finish purged the
caches of the others. Reproduced live: two concurrent reindexes; the later
completion committed a store path to a file the earlier completion's save
had already deleted (both jobs reported success — silent data loss).
Secondary path to the same graveyard: closing the app mid-parse (a 160 MB
SGY takes ~4 min) silently abandoned imports, persisting placeholder layers
that nothing ever repaired or flagged.

## Fixes (all root-cause level)
1. **Purge moved out of save()** — orphan cache cleanup now happens only in
   `Project::open()`, when no imports can be in flight. Orphans from deleted
   layers persist until the next open (D-04: artifacts are durable; deferred
   cleanup is fine). This kills the race outright.
2. **Completion guard** — completeImport/completeReindex refuse to commit a
   store path whose file no longer exists (defence against any other
   deleter): the layer fails loudly instead of referencing a ghost file.
3. **Self-heal at project open** — any layer persisted without a built
   index/parsed data is automatically re-imported through the normal reindex
   pipeline (progress UI, failure reporting, map hookup all reused). Old
   damaged projects repair themselves on open; permanently bad files fail
   into the Problems panel instead of sitting as silent dead lines.
4. **Close guard** — closing while imports run now warns (Close/Cancel,
   default Cancel), noting that abandoned lines re-import on next open.
   New `ExecutionController::importsBusy()`.

## Round 2 — recovered lines had no georeferencing (user screenshot)
After the first heal the recovered lines showed "No valid GPS position in
any trace" (36k bad nav). Cause: the original June import confirmed the
survey CRS in the wizard (EPSG:25828, exact) — but the reindex path had NO
WAY to receive a CRS, so recovery landed on the parser's "PROJECTED:SEGY"
placeholder and every trace failed nav normalisation.
- CRS plumbed through the whole reindex chain (ExecutionController →
  ImportJobManager (QueuedJob.source_crs, previously unused for rebuilds) →
  ImportService::reindexLayer → completeReindex applies it as exact).
- completeReindex CRS rules: caller CRS wins; else a previously confirmed
  (exact) layer/source CRS is preserved when the parser only auto-detected;
  else detection stands. A reindex can no longer LOSE georeferencing.
- Self-heal passes the source's exact CRS, falling back to the survey grid
  (first exact CRS among project sources) — mirroring the wizard's
  one-CRS-per-batch behaviour.
- The damaged project's manifest was repaired in place (EPSG:25828 applied
  to the two recovered lines + their sources; stale hidden flag cleared;
  .dlp.bak backup written).

## Verification
- Reproduced the loss live (concurrent reindexes → one .dlpd silently
  deleted by the sibling's save-purge) BEFORE the fix.
- Both 160 MB SGY reindexes complete cleanly (~4 min each, concurrent) —
  parser and concurrency are sound; the purge was the killer.
- After the fix: opening the damaged project self-heals the remaining dead
  line end-to-end (re-parse → store committed → project saved → map ribbon).
- Build green; 16/16 tests.
