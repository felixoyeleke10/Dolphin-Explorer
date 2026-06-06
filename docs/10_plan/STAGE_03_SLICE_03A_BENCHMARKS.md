# Stage 03 / Slice 03A — Benchmark Definitions and Baseline

**Slice:** 03A — Benchmark set and baseline capture  
**Status:** Open — baseline numbers to be filled in on reference workstation

---

## Purpose

Establishes the measurement framework for Stage 03 so that optimisation
work in 03B/C/D can be quantified rather than guessed.

No optimisation work should start in 03B/C/D without the baseline numbers
in this document being populated first.

---

## Reference Workstation

Fill in before running the baseline:

| Field | Value |
|-------|-------|
| CPU | ___ |
| RAM | ___ |
| Storage | ___ (NVMe / SATA SSD / HDD) |
| OS | ___ |
| Compiler | ___ |
| Build type | Release (-O2) |
| Qt version | ___ |

---

## Workload Classes

### W1 — Short survey line

~10 minutes of sidescan at 1 Hz dual-channel, 512 samples/side.

- 1 000 SSS pings (port + starboard = 2 000 entries)
- ~20 MB raw XTF

### W2 — Standard survey line

~2 hours of sidescan at 1 Hz dual-channel, 1024 samples/side.

- 7 200 SSS pings (~14 400 entries)
- ~150 MB raw XTF

### W3 — Full survey day

~8 hours of sidescan at 1 Hz dual-channel, 1024 samples/side.

- ~28 800 SSS pings (~57 600 entries)
- ~600 MB raw XTF

### W4 — Large project

10 survey lines (W2 class), one project with 50 layers, 500 contacts.

---

## Benchmark Scenarios

### A. `cache_build_index`

**What:** `ParsedCacheReader::open()` + `buildIndex()` on a DLPD file.  
**Why:** Every layer activation + every project open that hits a missing index
goes through this path. On W3 class it's the dominant open-cost today.  
**Scales:** 1k, 10k, 50k entries (synthetic DLPD, in-tree).

### B. `cache_seq_read`

**What:** Sequential `readArtifact()` over all entries in a 10k-entry DLPD.  
**Why:** Waterfall initial load reads artifacts sequentially; this measures
raw read throughput without OS-cache effects.  
**Scale:** 10k entries (synthetic).

### C. `project_from_json` / `project_to_json`

**What:** `Project::fromJson()` and `toJson()` on a project with N layers
and M contacts.  
**Why:** Project open parses JSON; save serialises it. Slowness here makes
project switches feel sluggish.  
**Scales:** 10L/100C, 50L/500C (synthetic, in-tree).

### D. `xtf_build_index` (real fixtures)

**What:** `XtfReader::buildIndex()` on the reduced real-vendor fixtures.  
**Why:** Establishes XTF parsing baseline with real (not synthetic) data.  
**Files:** `fix016_edgetech4200_isis_reduced.xtf` (24 KB), `fix017_tst500k_32bit_reduced.xtf`.

---

## Running the Benchmarks

```
cmake --build build --target test_perf_baseline
ctest --test-dir build -R PerfBaseline -V
```

Or directly:

```
./build/tests/test_perf_baseline
```

Output lines prefixed `PERF` are grep-friendly:

```
PERF  cache_build_index_1k        x.xx ms  n=1000     open+buildIndex
PERF  cache_build_index_10k       x.xx ms  n=10000    open+buildIndex
PERF  cache_build_index_50k       x.xx ms  n=50000    open+buildIndex
PERF  cache_seq_read_10k          x.xx ms  n=10000    readArtifact per entry
PERF  project_to_json             x.xx ms  n=10       10L 100C
PERF  project_from_json           x.xx ms  n=10       10L 100C
PERF  project_to_json             x.xx ms  n=50       50L 500C
PERF  project_from_json           x.xx ms  n=50       50L 500C
PERF  xtf_build_index_fix016...   x.xx ms  n=32       buildIndex
PERF  xtf_build_index_fix017...   x.xx ms  n=16       buildIndex
```

---

## Baseline Numbers

Run on reference workstation (fill in before 03B begins):

| Scenario | n | Baseline ms | Target ms | Notes |
|----------|---|-------------|-----------|-------|
| cache_build_index_1k  | 1 000  | TBD | — | |
| cache_build_index_10k | 10 000 | TBD | — | |
| cache_build_index_50k | 50 000 | TBD | — | |
| cache_seq_read_10k    | 10 000 | TBD | — | |
| project_to_json (10L) | 10     | TBD | <2 ms | |
| project_from_json (10L)| 10    | TBD | <5 ms | |
| project_to_json (50L) | 50     | TBD | <10 ms | |
| project_from_json (50L)| 50    | TBD | <20 ms | |

---

## Exit Criteria for 03A

- [ ] Reference workstation documented above
- [ ] Baseline table populated with real numbers
- [ ] No scenario fails correctness (all CHECK passes)
- [ ] Results committed alongside the harness
- [ ] Closure note written (`STAGE_03_SLICE_03A_CLOSURE.md`)

---

## Targets for 03B/C/D

These targets are aspirational until the baseline is populated.

**03B (responsiveness):**
- `cache_build_index_50k` < 50 ms (target: background-thread safe under 100 ms)
- `project_from_json` (50L) < 10 ms (target: imperceptible on project open)

**03C (rendering):**
- Swath repaint < 8 ms at 1920×1080 for a W2-class project
- Map pan/zoom response < 16 ms (60 fps cap)

**03D:**
- Layer activation cancellable within 100 ms of user navigation
- Closure note with actual before/after numbers from 03B/C

---

## Performance Hazards Noted During 03A Survey

The following structural issues were identified while writing the harness.
They are **not** optimised here (that belongs in 03B/C) but are documented
so 03B has a starting list.

1. **`buildIndex` reads every record header sequentially** — no skip-ahead for
   files with known record sizes.  On a W3-class DLPD (600 MB) this could
   dominate project-open time.

2. **`Project::fromJson` rebuilds the artifact index from the binary cache
   inside the JSON parse** (line ~205 of `Project.Serialization.Read.cpp`).
   If the cache is large this blocks the open call.  This could move to a
   lazy background index-rebuild triggered by first layer activation instead.

3. **`writeParsedCache` and `writeArtifactBufferToCache` both call
   `payloadSize()` and then write in two passes** — one for the record header
   and one for the payload.  A single-pass write with pre-computed size would
   reduce syscall count.

4. **No precomputed bounding box in the DLPD index** — map-view first paint
   requires scanning all index entries to compute the project extent.  A
   single extent record in the file header would make this O(1).
