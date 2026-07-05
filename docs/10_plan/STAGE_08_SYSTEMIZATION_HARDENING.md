# Stage 08 — Systemization Hardening

## Why this stage exists

External architecture review (2026-07-04) graded the app's systemization B-/B
against mature hydrographic packages (SeaView/Moga class). A code survey
confirmed the direction but sharpened the gaps — several are smaller than
graded, one is larger:

**Already in good shape (verified, no work needed):**
- Layer discipline `core → io/geo → pipeline → app → ui` is real and enforced;
  the domain model is UI-independent and testable headless.
- DisplayStateManager already owns ALL per-layer display-struct mutation
  (SSS/SBP palettes, gain, signal, display, nav) — grep confirms zero direct
  writes outside it, with ONE exception (visibility; see S-96).
- Project manifest already has `version` (currently 10) with real migration
  branches at the v5 boundary in `Project.Serialization.Read.cpp`.
- Real coordinator objects exist: ProjectSessionController,
  LayerDisplayCoordinator (selection + nav history), ProjectOperationCoordinator,
  ViewportCoordinator, CorrectionBatchOperator.
- `test_project_storage.cpp` is a genuine headless workflow test
  (save/open/invalidate/remove round-trips, 120 checks).

**Confirmed gaps (this stage's slices):**
1. Schema version is magic-numbered (`10` in Write, `5` in Read) and has NO
   forward-compatibility guard — a `.dlp` written by a future version opens
   silently and misparses instead of failing cleanly.
2. Layer visibility bypasses DisplayStateManager: `onLayerVisibilityChanged`
   (MainWindow.Layout.cpp) writes `layer->visible` directly inside its undo
   apply-lambda and fans out to 4 widgets by hand. Last remaining direct
   display-state write in the UI layer.
3. No display-state round-trip test: palettes / gain / nav / visibility are
   serialized but nothing proves they survive save→reopen.
4. MainWindow is still the application brain (~9,000 lines across aspect
   files; the `coordinators/MainWindow.*.cpp` files are member-function splits,
   not ownership splits). Multi-slice; sequenced last because 1–3 de-risk it.

## Slices

### S-95 — Schema version constants + forward-compat guard
- `Project.h`: `static constexpr int kSchemaVersion = 10;` (+ short doc of the
  version history and the bump rule: any serialization change bumps it and
  adds a read-side branch).
- Write side uses the constant; read side refuses `version > kSchemaVersion`
  with a distinct error the UI can present ("project was saved by a newer
  version of Dolphin Explorer").
- Tests: round-trip at current version; synthetic future-version manifest is
  rejected; legacy low-version manifest still opens.

### S-96 — Visibility through DisplayStateManager
- `onLayerVisibilityChanged`'s undo apply-lambda calls
  `m_display_state->setLayerVisible()` (single mutate point, marks dirty,
  emits `displayStateChanged(id, Visibility)`) instead of poking the model.
- Widget fan-out (viewport, line list, layer picker) moves to the existing
  `displayStateChanged` consumer in MainWindow, keyed on `Visibility`.
- Undo/redo must still work (the command replays through the same setter).

### S-97 — Display-state round-trip workflow tests
- Extend `test_project_storage.cpp` (or a sibling `test_display_state.cpp`):
  set sss/sbp palettes, sbp gain/contrast/invert, nav params, visibility on
  layers → save → reopen → assert every field. This is the regression net for
  the DisplayStateManager migration and future serialization changes.

### S-98+ — MainWindow decomposition (sequenced, not scheduled)
Extraction order (each its own slice, each behind the S-97 regression net):
1. Layer visibility + selection fan-out into LayerDisplayCoordinator
   (it already owns selection state; give it the widget-sync job).
2. `MainWindow.Tools.cpp` (683 lines) → ToolController owning tool state.
3. Import wiring (`MainWindow.Import.cpp`) → extend ImportController.
Rule for new code effective now: **new wiring goes into a coordinator/
controller object, not a MainWindow member function.** MainWindow composes.

### Deferred (recorded, deliberately not scheduled)
- Plugin/module boundaries, command bus/action registry: ceremony at current
  team/product size; revisit post-1.0.
- QTest-driven widget tests: offscreen GL works (GlSmoke proves it) but the
  cost/benefit loses to headless app-layer workflow tests today.

## Exit criteria
- No direct display-state writes outside DisplayStateManager (grep-clean).
- Future-version manifest rejected with a user-presentable error; tests cover
  reject + legacy-open paths.
- Display state provably round-trips (tests in CI).
- Closure notes per slice in `docs/90_closure_notes/`.
