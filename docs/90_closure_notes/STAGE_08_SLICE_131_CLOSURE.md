# Stage 08 Slice 131 Closure — Cross-Window State Ownership

## Behavioral goal

Prevent one viewer or editor surface from silently overwriting another surface's draft controls or display palette.

## Delivered

- Added per-layer Waterfall-window parameter drafts; switching lines preserves edits and restores them when returning to that line.
- Added per-layer main-window SSS and SBP editor drafts; changing layer context no longer destroys in-progress Gain, Imaging, SBP Gain, or SBP Signal values.
- Removed Waterfall-to-main-panel mirror writes on Apply and Open.
- Removed Sub-bottom-open writes into SBP Gain and Signal editor modules.
- Restricted Waterfall reload-on-selection to sidescan layers only.
- Split Waterfall palette persistence to `waterfall/paletteIdx`; it no longer shares the map's `sss/paletteIdx` key.
- Removed palette fanout from Waterfall to the map/SBP viewer and from map/default palette changes to Waterfall/SBP viewers.
- Generic app-settings broadcasts no longer reapply viewer palettes or force viewer rerenders.
- Scoped CRS assignment from the Waterfall picker to the selected source and its derived layers instead of every layer in the project.

## Explicit broad actions retained

- **Apply to All** remains project-wide for the active modality.
- **Nav to All Lines** remains project-wide for sidescan navigation parameters.
- Main-window **Apply to Line** still updates an open viewer because the user explicitly initiated that application.

## Verification

- Full `DolphinExplorer` MSVC/Ninja compile and link: passed.
- `git diff --check`: passed.
