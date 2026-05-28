# Stage 03 - Performance and Scalability

This stage is for systematic speed work after the foundation and compatibility stages are stable.

It exists to answer a different question:

- not just "does it work?"
- but "does it stay responsive and scale like serious survey software?"

## Important Boundary

This stage is not where all performance work begins.

Earlier stages should still fix obvious performance blockers when they are really architecture problems.

Examples of blocker-level work that belong earlier:

- full-line decode on simple layer activation
- eager startup loading of every indexed sidescan layer
- UI-thread heavy work in import/open/display paths

Stage 03 is for deliberate, measured optimization after those structural blockers are already being addressed.

## Entry Criteria

Stage 03 should begin only when performance work can be measured against stable-enough behavior.

That means:

- the biggest architectural performance blockers from Stage 01 are already removed or contained
- Stage 02 has produced representative file coverage so benchmarks reflect real workloads
- the team agrees which workloads matter most before tuning begins

## Goal

Make the software feel fast and predictable on real survey workloads:

- large XTF files
- many indexed layers
- repeated project open/close cycles
- large map extents
- dense swath rendering
- long processing sessions

The target is not just raw throughput.

The target is:

- low startup friction
- responsive interaction
- predictable memory use
- graceful scaling as project size grows

## What This Stage Should Cover

## 1. Profiling and Benchmarking First

Before optimization work, define and capture baseline metrics for:

- project open time
- first-layer activation time
- time to first visible map content
- import/index time by file size
- cache-open time
- map zoom/pan responsiveness
- memory usage during large-line activation
- cost of loading many layers

Optimization without baselines will turn into guesswork.

## 2. Project Open Performance

Project open should avoid doing more than is necessary to establish a usable workspace.

Focus areas:

- avoid eager decode of full sidescan data
- load metadata and extents before heavy payloads
- defer non-visible content
- make already-indexed projects reopen quickly

Desired behavior:

- open project quickly
- show navigable context early
- deepen data loading progressively

## 3. Layer Activation Performance

Selecting a layer should not require decoding the entire raw/cached line before the UI responds.

Desired direction:

- activate layer from index/extents immediately
- decode only the visible or nearby window first
- continue higher-resolution loading in the background
- support cancellation when the user switches layers quickly

This is one of the biggest user-perceived speed wins.

## 4. Rendering Performance

Map rendering should scale with viewport need, not full dataset size.

Areas to optimize:

- swath level-of-detail
- viewport culling
- tile or strip caching
- redraw invalidation discipline
- progressive refinement at high zoom

The renderer should avoid recomputing or repainting everything on small interactions.

## 5. Cache Strategy for Speed, Not Just Persistence

The parsed cache should be treated as a performance product, not only a persistence artifact.

Potential improvements:

- faster startup metadata summaries
- lightweight map-activation records
- precomputed extents and counts
- precomputed display-ready windows or tiles where justified

The goal is to reopen and react quickly without needing to fully rehydrate raw artifacts every time.

## 6. Concurrency and Responsiveness

Heavy work should move off the UI thread wherever possible.

Areas to review:

- layer activation
- cache reading for large lines
- background normalization
- progressive map enrichment
- import and reindex post-processing

But concurrency should be measured and owned carefully.

Bad concurrency creates:

- race conditions
- stale UI updates
- hard-to-reproduce state bugs

So this stage should emphasize:

- cancellation
- ownership
- bounded background work
- UI-safe completion paths

## 7. Memory Discipline

Speed is not only CPU time.

The system should avoid unnecessarily retaining:

- full decoded ping sets
- duplicate normalized copies
- too many simultaneously loaded swath layers
- oversized render caches

The app should have a clear policy for:

- what stays resident
- what can be dropped
- what gets recomputed
- when background caches are invalidated

## 8. Scalability Test Scenarios

This stage should define real workload classes, for example:

- one large XTF line
- many medium XTF lines
- mixed-modality project
- large project reopened from cache
- fast switching between multiple indexed layers
- deep zoom on dense swath content

Without those scenarios, "performance" will stay anecdotal.

## Recommended Deliverables

## 3.1 Performance Baseline

Create a repeatable benchmark set with:

- fixture files
- benchmark scenarios
- target metrics
- before/after comparison tables

## 3.2 Responsiveness Improvements

Deliver user-visible wins first:

- faster project open
- faster first-layer display
- less UI freezing during layer changes
- better map interaction under load

## 3.3 Scalability Architecture

Then harden the system around:

- progressive loading
- background decode
- cancellation
- smarter cache usage
- resident-memory limits

## Concrete Deliverables

Stage 03 should finish with implementation artifacts that can be reviewed:

- a repeatable benchmark set with named workload scenarios
- a baseline-versus-after performance report
- a reference workstation note with hardware, storage, dataset class, and benchmark scenarios
- a documented activation/loading strategy for visible-first data
- cancellation or interruption behavior for long-running activation/loading work
- a documented memory-residency policy for decoded data, render caches, and background products

## Provisional First-Pass Targets

Until a human explicitly revises them, Stage 03 should use these targets on the chosen reference workstation:

- representative cached project open to navigable context: `<= 5 seconds`
- representative indexed layer activation to first visible content: `<= 2 seconds`
- no standard pan/zoom action on benchmark scenarios should cause visible UI unresponsiveness longer than `250 ms`
- heavy import/decode queueing should be bounded and documented with a default limit of `2` simultaneous heavy jobs
- repeated `10`-layer switch benchmark should return resident memory to within `20%` of post-warm steady-state after cleanup

## Acceptance Criteria

Stage 03 should be considered successful when:

- large-project open time is measured and improved
- first visible content appears quickly after project open
- layer activation no longer feels blocked by full-line decode
- map pan/zoom remains responsive on large datasets
- memory growth is bounded and explainable
- benchmark scenarios exist and are repeatable
- optimization claims are supported by numbers, not impressions
- the provisional first-pass targets are either met or explicitly revised by a human-backed benchmark note

## Relationship To Earlier Stages

- Stage 01 fixes architectural and shell drift that would make optimization unsafe.
- Stage 02 expands XTF compatibility so speed work applies to a broader real-world file set.
- Stage 03 turns the system into something that feels fast at workstation scale.
- Stage 04 then cleans up import/reuse policy so the faster system also behaves predictably as a registered-data workflow.

## Bottom Line

Speed optimization deserves its own stage.

But it should be treated as measured scalability work, not random micro-optimization.

The right path is:

- fix structural blockers early
- broaden real XTF capability
- optimize with benchmarks and workload-driven goals
- then tighten import behavior on top of the faster, broader foundation

## Out Of Scope For Stage 03

- do not treat this as a substitute for missing compatibility work
- do not turn it into random micro-optimization without baselines
- do not let it redefine source-identity or import-policy rules that belong to the later workflow stage
