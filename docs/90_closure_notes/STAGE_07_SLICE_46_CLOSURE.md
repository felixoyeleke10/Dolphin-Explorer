# Stage 07 — Slice 46: Persist SSS imaging params (fix slow project reopen)

## Symptom
After applying gain/imaging settings to SSS data, reopening the recent project was
slow.

## Root cause
Slice 45 added the gain/imaging params to the SSS map raster-cache fingerprint
(`SidescanRasterCache::makeMeta`) so changing a tool re-rasterizes. But the project
serializer (`Project.Serialization[.Read].cpp`) only persisted TVG/ARC/AGC + gain/
contrast/threshold/SRC — **not** the imaging chain (ARN / Destripe / Beam-Pattern /
ML-Enhance). So when those were enabled:

- Apply built + saved a raster keyed on the real imaging params.
- On reopen the layer's imaging params fell back to disabled (not persisted), so the
  fingerprint no longer matched the saved raster → **cache miss → full ping decode +
  re-rasterize for every affected layer on every open** (the slowdown). The applied
  imaging corrections were also silently lost.

## Fix
Persist the full imaging chain in `sss_display` (write + guarded read):
- `arn_en/arn_str/arn_cap/arn_smooth`
- `ds_en/ds_win/ds_sub/ds_cap`
- `bpn_en/bpn_str/bpn_rad`
- `ml_en/ml_tp/ml_ts/ml_clip`

Read side guards each block with `jd.has(...)` so projects saved before these keys
load fine (missing → disabled defaults). Now the reopened params match the Apply-time
raster fingerprint → cache hit → instant open, and the corrections survive a reload.

## Files
- `src/app/project/Project.Serialization.cpp`
- `src/app/project/Project.Serialization.Read.cpp`

## Verification
- Build green (incl. tests).
- NEEDS VISUAL CHECK: apply an imaging tool (e.g. ARN), save, reopen the project →
  opens fast (cache hit) and the correction is still applied on the map.

## Note
Float params round-trip float→double(JSON)→float exactly for the slider-stepped
values the UI produces, so the byte-hash in makeMeta matches across save/load (same
mechanism nav params already rely on).
