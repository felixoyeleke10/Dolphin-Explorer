# Stage 07 Slice 21 — Project-open perf: footer persistence + progress flood

Timing instrumentation pinned the "open takes a minute / stuck 10 min" complaint:
```
[timing] Project::open (manifest + footer quickIndex) = 43 ms      ← fast
[timing] open: 0 layer(s) indexed from footer (fast), 4 need background rescan (slow)
```
Project::open itself is fast. The cost is that **all 4 layers full-rescan every open**,
because the compact index footer never persists (0/4), so the cache never reaches the
fast `quickIndex` path.

## Three root causes (in `io/cache/ParsedCache*`)
1. **Footer never persisted (the big one).** `buildIndex` appends the footer by
   opening a *second* handle (`"r+b"`) to the `.dlpd` while the read handle is still
   open. On Windows the default-share read handle made that append fail silently → no
   footer → every open re-scans forever. Fixed by opening **both** handles shared:
   `_fsopen(path, "rb"/"r+b", _SH_DENYNO)`. Now the footer write succeeds while the
   reader (e.g. `ImportService.Load` keeps it open) is live, so after one scan every
   subsequent open takes the fast footer path.
2. **Progress event flood → UI freeze / "stuck".** `buildIndex` called `progress()`
   **per record** — tens of thousands of `QMetaObject::invokeMethod` posts per layer,
   ×4 concurrent scans, drowning the main thread (couldn't even move the panel).
   Throttled to whole-percent steps (~100 calls instead of ~50k).
3. **Tiny default read buffer.** Added `setvbuf(_IOFBF, 1 MB)` so a full scan reads in
   large sequential chunks — much faster on spinning/network disks.

## Result
- First open of a footerless project: one **responsive** background scan (throttled
  progress, buffered reads) that now **writes a footer**.
- Every open after that: `quickIndex` footer read → ~tens of ms. No reparsing.

## Also (this batch)
- Lazy project open (Slice 20): only the selected layer loads into the map, not all.
- Timing logs kept for now so the fix can be verified (`[timing]` in the Output tab):
  open #1 should show "4 need rescan", open #2 should show "4 from footer (fast)".
  Remove the logs once confirmed.

## Build / tests
`dolphin-io` compiles; ParsedCache (footer round-trip) + PerfBaseline pass. Exe relink
waits for the app to close. Verify: rebuild, open the project, **close and reopen** —
the second open should be near-instant and the Output log should show 4 layers from
footer.

## Two more open-path fixes (the 12-minute screenshot)
The 12-min open log showed it building the map raster for **all 4 layers** — the
lazy-open (Slice 20) was defeated because the reindex-completion handler eagerly
activated every reindexed layer.

- **Only the active layer's mosaic builds on open.** `MainWindow`'s
  `cacheLayerReady` handler previously called `activateLayer(...)` for *every* layer
  whose index finished rebuilding → N mosaics. Now it activates the map only for the
  active layer (or the first, if none selected); other reindexed layers stay indexed
  but unloaded until selected. This removes the bulk of the open time (raster builds).
- **Reindex scans are serialized.** `ImportService::rebuildCacheIndex` now queues and
  dispatches `kMaxConcurrentRebuilds = 1` at a time (`startRebuild`/`pumpRebuilds`),
  instead of launching N concurrent `QtConcurrent::run` full scans that thrash a
  spinning disk. `cancelPendingRebuild` clears the queue.

### Net open behaviour
- First open of a footerless project: N **sequential** reindex scans (responsive,
  buffered, each now writes a footer) + **one** mosaic build (the active layer).
- Every open after that: N fast footer reads + one mosaic build → seconds.

All libraries compile. **The running exe was several builds behind** (rows still said
"artifacts cached", maps built for all 4) — a full rebuild is required to pick up
lazy-open + footer + cacheLayerReady + serialized scans together.

## Survey overview without the cost (the "looks empty" follow-up)
After lazy-open, the map showed only the selected line — the user expected to see the
whole survey. Fixed without bringing back the per-line mosaic cost:
`SidescanViewController::showNavTrackFromIndex(layer)` draws a line's **nav track
straight from the in-memory artifact index** (per-entry lat/lon, thinned to ~1000
points, reprojected to the display CRS via `geo::normalizeNavForMap`) — **zero ping
I/O, no raster**. On open, every non-active indexed sidescan line gets its track this
way (instantly, or as its background reindex completes via `cacheLayerReady`), while
the active line still builds its full mosaic. Selecting a line replaces its track with
the mosaic. This is the SeaView model: all track lines visible immediately, click one
for its imagery.
