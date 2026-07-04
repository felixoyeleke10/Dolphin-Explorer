# Stage 07 · Slice 91 — 3D SBP curtain shows the REAL profile

## Goal (user QC)
"This is not my SBP data — and it's not even connected to the palette!"
The 3D curtain was garish blue/red streaks unrelated to the profile.

## Root causes (all three real)
1. **Not the data**: the curtain carried ONE value per trace — the amplitude
   at the bottom-pick sample — interpolated from 0 at the waterline to that
   value at the seabed. A per-ping bottom-amplitude smear, not the profile.
2. **Unnormalized**: raw |sample| in native units fed a shader that clamps at
   1.0 — arbitrary saturation (the random yellow/red streaks).
3. **Palette disconnected**: hardcoded default `palette_index = 3` (Thermal)
   with a hand-rolled GLSL gradient; never linked to the SBP display palette.

## What changed — textured fence with the real profile
- **SbpProfileBuild**: rasterizes the ACTUAL trace samples into
  `LayerMapData::curtain_image` (≤4096×512 RGBA: R = |amplitude|
  percentile-normalized 2–98% across the line — same stretch idea as SSS;
  A = 0 below each trace's data / gap columns) + `curtain_depth_m` (full
  profile depth). Column↔trace mapping tracked explicitly (repeated-fix
  filtering means nav entries ≠ trace indices).
- **MapView3D**: curtain geometry is now UV-mapped quads spanning the FULL
  profile depth (5 floats/vertex); the raster uploads as a GL texture
  (construct-before-destroy, drape pattern); the fragment shader samples it,
  discards transparent texels (ragged bottom follows real data), and applies
  the SbpPalette per-fragment. `setCurtainPalette(idx)` recolours every
  curtain via a uniform — zero rebuild. Texture freed in remove/clearScene/
  teardown paths.
- **Palette wiring**: displayStateChanged(Palette, SBP layer) →
  `MapViewportHost::setSbpCurtainPalette` (covers the Views SBP tab, the SBP
  viewer's display panel, and the inspector); the curtain build completion
  seeds the initial palette from the layer (`sbp_palette`, default
  Greyscale — matching the SBP viewer default, not Thermal).

## Verification
Build green; 16/16 tests. Live on the SBP-Only survey (3 lines): 3D shows a
greyscale textured fence with the surface-return band, ping striping, and
real amplitude structure — recognisably the same profile as the SBP viewer.
Orbit (right-drag) and wheel zoom exercised via synthetic input during the
check. Note: at true scale an ~11 m profile under km-long lines is thin —
vertical exaggeration (existing 3D setting) is the intended way to inspect it.
