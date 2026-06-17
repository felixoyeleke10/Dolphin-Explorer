# Stage 07 Slice 10 — Perf fix: don't throttle map-display builds under the D-14 cap

## Symptom
After the OperationManager migration went fully live (exe finally relinked),
parse→display on the map became "significantly slow."

## Root cause
The migration flagged the **primary per-layer map-display builds** as `heavy`,
putting them under the D-14 heavy-job cap (`m_heavy_cap = 2`):
- `SidescanViewController::activateLayer` (SSS map build) — `heavy=true`
- `MainWindow::buildSbpProfileMap` (SBP profile-map build) — `heavy=true`

These two share the single cap, so **all map-display builds ran ≤2 at a time**.
On a multi-line survey project (many layers) on a multi-core machine this starved
display to 2 cores where it was previously pool-wide (≈`idealThreadCount`), i.e. a
~4× wall-clock regression on the display phase. (Single-layer load is unaffected —
1 job < cap, so this only bit multi-layer projects.)

Note the actual **import decode** (`ImportService`) uses raw `QtConcurrent::run`
(global pool), *not* this cap — so the cap was throttling display, not decode.

## Fix
Made both primary display builds **not heavy** (`heavy=false`):
- `src/ui/features/map/sidescan/SidescanMapLoadTask.cpp` (activateLayer)
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp` (buildSbpProfileMap)

Keyed supersession + cancellation token + `on_finally` (busy counter) are
unchanged — only the cap/queue is bypassed. The global `QThreadPool` still bounds
total concurrency, so this restores pre-migration parallel display without
spawning unbounded threads.

Still `heavy` (correctly — secondary/background or single-window): `prebuildTier`
(ProcessingWindow), SBP/waterfall window loads.

## D-14 interpretation (flag for review)
D-14 caps "heavy **import/decode** jobs" at 2. A per-layer **display** build
re-reads the already-built `.dlpd` artifact store and rasterizes for the map — it
is display prep, not the import/parse pipeline — so it should not sit under the
import/decode cap. This change treats display builds as out-of-scope for D-14
rather than altering the cap value (DECISION_LOG untouched). If the project
considers map builds to be "decode," the alternative is to keep them heavy and
raise the cap — but that would change the D-14 number, so it was not done.

## Build
Both libs compile; exe relink blocked only by LNK1168 (app running) — close the
app to relink. No code error.

## Runtime-verify (user)
Open/import a multi-line project and watch parse→display: layers should now build
in parallel (pool-wide), not 2 at a time. If a *single* layer is still slow, the
bottleneck is the decode/rasterize itself (unchanged code) — a separate
investigation.
