# Stage 05 Slice 01 — ImportJobManager D-14 Concurrency Cap

## Goal
Bring `ImportJobManager` into compliance with D-14 (max 2 concurrent heavy import/decode jobs) and add per-batch outcome counts to `batchCompleted`.

## What Was Done

### D-14 cap (serial → concurrent)
- Replaced `bool m_busy` with `int m_active_count` + `static constexpr int kMaxConcurrent = 2`.
- Replaced `bool m_awaiting_start` with `int m_awaiting_start_count` to handle multiple concurrent jobs waiting for their first signal.
- Replaced `std::string m_active_layer_id` with `std::map<std::string, ActiveJob> m_active_jobs` where `ActiveJob = { Kind, bool started }`.
- `dispatchNext()` is now a `while (m_active_count < kMaxConcurrent)` loop — up to 2 jobs run concurrently.

### Why `ActiveJob::started` flag
`RebuildExisting` jobs have a known layer_id at dispatch time (pre-inserted into `m_active_jobs`). If a rebuild fails synchronously (before `indexingStarted` fires), `m_awaiting_start_count` must still be decremented. The `started` flag lets `onIndexingFailed` distinguish "failed before started" from "failed after started" regardless of whether the entry was pre-inserted.

### Missing-source guard
`dispatchNext()` calls `QFileInfo::exists()` before dispatching any `RebuildExisting` job. Missing-source jobs are counted as `failed` in the summary and skipped without holding a concurrency slot.

### BatchSummary
Added `BatchSummary { imported, rebuilt, reused, failed }` struct. `batchCompleted()` now carries the summary. Counters accumulate during a batch and reset on emission.

### Signal chain
`ImportJobManager::batchCompleted(BatchSummary)` → `ExecutionController` lambda slot → re-emits `ExecutionController::batchCompleted(BatchSummary)`. No downstream consumers yet, but the type propagates correctly.

## Invariants Maintained
- All internal state updates (count decrements, map erases, `dispatchNext` re-dispatch) run unconditionally regardless of the epoch check.
- UI signals (`jobStarted`, `jobCompleted`, `jobFailed`, `statusMessage`) are suppressed when `m_active_job_epoch != m_epoch` (post-cancel).
- `cancelQueue()` only emits `batchCompleted` immediately if no jobs are in-flight; otherwise defers to the natural completion path.

## Files Changed
- `src/app/import/ImportJobManager.h` — redesigned concurrency tracking fields + `BatchSummary` struct
- `src/app/import/ImportJobManager.cpp` — full rewrite of dispatch loop and signal handlers
- `src/ui/features/import/ImportController.h` — `batchCompleted()` → `batchCompleted(BatchSummary)`
- `src/ui/features/import/ImportController.cpp` — lambda slot bridges the summary through

## Tests
`test_import_classifier` — 19 passed, 0 failed (unchanged; does not exercise concurrent dispatch but validates dedup, reuse, and project integration).

## What Remains
- Progress dialog (`ImportProgressDialog`) still shows "Task N of M" using `m_queue_total`; it doesn't yet show per-slot concurrent progress rows simultaneously. That's a UI-only concern and doesn't affect correctness.
- `batchCompleted(BatchSummary)` has no downstream display consumer yet — `MainWindow.cpp` doesn't connect to it. Wiring the summary into a toast/notification is Stage 05 Slice 02 territory.
