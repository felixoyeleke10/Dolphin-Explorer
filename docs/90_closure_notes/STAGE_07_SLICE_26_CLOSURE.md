# Stage 07 Slice 26 — Import: honest count + CPU-scaled concurrency (D-14 revised)

## Symptom
Importing 3 SBP lines showed "Importing 3 line(s)" but "Reading 0 of 2 lines" with only
2 rows — looked like the 3rd was dropped.

## Two parts

### 1. Display under-count (bug)
`updateHeader` used the true total (`max(m_queue_total, rows)` = 3) but `updateStages`
used only `m_rows.size()` (= 2 dispatched rows), so the stage line said "of 2" and could
flip `reading_done` true before the queued line was even added. Fixed `updateStages` to
use the same true total. The 3rd line was always being imported (it dispatches when a slot
frees); only the readout was wrong.

### 2. Concurrency cap raised (owner directive)
> "it should be professional to handle as many data as possible, shouldn't be capped."

`ImportJobManager` parallelism was a fixed `kMaxConcurrent = 2` (D-14). Replaced with
`maxConcurrentJobs()` = `std::thread::hardware_concurrency()` (floor 2): a batch now uses
every logical core. Beyond the core count the work still queues (parsing is CPU-bound, so
more than cores only thrashes disk/scheduler; unbounded would risk OOM on big batches).
So any number of files imports in one batch, limited only by the hardware.

**D-14 revised** in `docs/00_control/DECISION_LOG.md` (Status: Locked, revised
2026-06-17 at owner direction; original fixed-2 kept under History). The visible-queue
rule is retained.

## Scope note
This is the import/decode path (what the user hit). The OperationManager map-build and
correction-bake lanes are still capped at 2 to keep the UI responsive during display
rasterization / baking — separate concern; scale those only on request.

## Build
Full build green, exe relinked (needs clean relink: close app → build_quick.bat).
