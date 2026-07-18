# Stage 08 Slice S-102 — SSS Cross-Window Processing QC

## Authoritative contract

- `DataLayer::sss_display_state` owns per-layer gain and imaging state.
- `DataLayer::nav_state` owns per-layer navigation state.
- Palette is the only global SSS rendering setting.
- User Apply is the commit boundary; programmatic synchronization is signal-silent.
- Map and waterfall consume the same stored per-layer parameters.

## Confirmed failures

1. Waterfall draft edits scheduled processing before the draft was copied into the renderer state.
2. Main-window Apply called a waterfall consumer method that re-emitted user Apply signals, duplicating persistence and rebuild work.
3. The Apply-All consumer path could widen an already-scoped main-window operation.
4. External waterfall synchronization read retired `waterfall/paletteIdx` instead of canonical `sss/paletteIdx`.
5. Waterfall Apply pushed one line's gain/contrast into the map controller's global display params, affecting all loaded layers.
6. The waterfall command-palette single-line Apply changed its private renderer without emitting the normal commit signal.

## Corrected behavior

- Waterfall edits remain drafts until Apply.
- Every waterfall Apply entry point emits exactly one scope signal.
- External waterfall synchronization emits no Apply signals.
- Main-window Apply preserves its computed target set.
- Map processing reads each layer's stored state; only palette uses global propagation.

## Non-goals

- No change to the explicit Bake Corrections command or durable `.dlpd` ownership.
- No change to SBP processing behavior in this SSS-focused slice.
