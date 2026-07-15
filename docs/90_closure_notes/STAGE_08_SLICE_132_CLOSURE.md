# Stage 08 Slice 132 Closure — Waterfall Draft/Applied Processing Separation

## Behavioral goal

Guarantee that Waterfall Destripe and other processing drafts cannot execute merely because a line loads, reloads, scrolls, or finishes an asynchronous operation.

## Root cause

The analysis panel correctly retained per-layer drafts, but load and repipe completion callbacks called `pushParams()`. That function reads the controls, so an unapplied Destripe draft was promoted into the live processing pipeline after background work completed.

## Delivered

- Split incoming Waterfall state into control drafts and last-applied view parameters.
- Asynchronous load staleness now compares against the view's applied parameters, not analysis-panel drafts.
- Removed draft promotion from load, stale-load repipe, cache repipe, and navigation-processing completion callbacks.
- Palette, display-channel, and Waterfall-settings updates now modify only their owned live fields instead of pushing the entire processing panel.
- Destripe is opt-in for every loaded line/session; legacy persisted `destripe.enabled=true` values written by the former feedback loop cannot activate it on open.
- `pushParams()` is now reachable only from explicit Waterfall Apply commands.

## Verification

- Full `DolphinExplorer` MSVC/Ninja build: passed.
- `git diff --check`: passed.
