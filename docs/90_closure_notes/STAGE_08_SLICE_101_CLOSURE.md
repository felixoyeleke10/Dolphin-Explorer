# Stage 08 — Slice 101 Closure: SBP curtain honours per-layer opacity

## Why
QC of slice 100 found the Views ▸ SBP Transparency control was a dead stub
(D-05 violation): `MapView3D::setLayerOpacity` only touched drape layers, but
an SBP line's map presence is a 3D **curtain** (`m_curtain_layers`), and the
curtain fragment shader hardcoded a fixed `0.92` alpha with no opacity uniform.
SBP also has no 2D preview image, so the 2D opacity path skipped it too. The
spinbox moved and persisted `map_opacity` but changed nothing visible.

## Fix
- Curtain fragment shader (`MapView3D.GL.cpp`): new `uniform float uAlpha`;
  `fragColor` alpha is now `0.92 * uAlpha`. New `m_loc_curt_alpha` location.
- `CurtainLayer3D::opacity` (default 1.0), seeded once from `data.opacity` in
  `setProfileCurtain` (same construct-time-seed pattern as `SonarDrape3D`).
- `drawCurtains()` sets `uAlpha` per curtain from `C.opacity`.
- `MapView3D::setLayerOpacity` now also updates matching `m_curtain_layers`
  (drapes were already handled) so live edits fade the curtain immediately.
- No GL state change needed: `GL_BLEND` + standard `SRC_ALPHA / ONE_MINUS_
  SRC_ALPHA` are already enabled globally (the pre-existing 0.92 curtain alpha
  relied on it), so `0.92 * uAlpha` just blends further.

## Verification (temp diag, removed after)
SBP-only project, first layer selected: `curtainLayerCount()==3`, curtain
opacity read back 1.0; `setLayerOpacity(id, 0.35)` → model 0.35 AND the curtain
layer's stored opacity 0.35 (feeds `uAlpha`). Confirms the value reaches the
render path, not just the model. Temp accessor + diag removed after capture.

Full suite 16/16 green. slice-100 closure note corrected (its "applies to the
3D curtain" claim is now actually true).

## Follow-ups recorded (not done — need explicit go-ahead)
From the SeaView reference, still deliberately unimplemented for lack of a
renderer path: Blend mode, Coverage-only rings, Clipping polygons, Show
discarded areas, Show side-scan beams, Interval, Dynamic-range histogram. Of
these, Blend mode and the Dynamic-range histogram are the most reachable (the
percentile stretch that would feed the histogram already exists) and are the
sensible next slice if the user wants to keep closing the gap.
