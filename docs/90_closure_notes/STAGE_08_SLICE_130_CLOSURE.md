# Stage 08 Slice 130 Closure — Side-Effect-Free Viewer Restoration

## Behavioral goal

Ensure opening, reloading, or switching viewer lines restores state without silently applying or persisting settings as though the user pressed Apply.

## Delivered

- Waterfall now establishes the incoming layer's complete processing state before asynchronous loading begins.
- Uncustomized Waterfall layers receive canonical defaults instead of inheriting TVG, AGC, ARN, Destripe, BPN, ARC, or enhancement controls from the outgoing line.
- Removed programmatic calls through `applyExternalParams()` during open, layer selection, sound-velocity reload, and CRS reload. That method is now reached only by explicit Apply workflows.
- Programmatic Waterfall panel synchronization blocks processing-toggle signals and preserves the stored SRC value exactly; BPN restoration no longer silently forces SRC on.
- Sub-bottom display updates now distinguish user persistence (`notifyParamsChanged`) from side-effect-free synchronization (`refreshParams`).
- Opening the Sub-bottom viewer, restoring a layer, synchronizing palette, and synchronizing sound velocity no longer rewrite global `sbpDisplay/*` preferences.
- Added a dedicated `restoreDisplayParams()` path for per-layer Sub-bottom restoration.

## Verification

- `cmake --build . --target dolphin-ui-waterfall dolphin-ui-subbottom --parallel 1` (MSVC/Ninja): passed.
- `cmake --build . --target DolphinExplorer --parallel 1` (MSVC/Ninja): passed.
- `git diff --check`: passed.

## Persistence boundary

- Programmatic open/restore/sync: updates controls and live rendering only.
- Explicit viewer or main-panel Apply: may update project display state and processing output.
- Direct lightweight user controls: continue to persist only in response to the user's edit.
