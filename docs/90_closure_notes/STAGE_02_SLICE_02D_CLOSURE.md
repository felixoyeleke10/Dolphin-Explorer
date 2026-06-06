# Stage 02 — Slice 02D Closure Note

**Slice:** 02D — Real-vendor validation and fixture reduction
**Closed:** 2026-06-03
**Status:** Complete

---

## What this slice delivered

This slice satisfies the Stage 02 plan's "≥2 prioritized real vendor/recorder
families" bar — the last outstanding acceptance item — by reducing two real
XTF captures into checked-in fixtures and validating them against the parser.
No production code changes were required.

### 1. Fixture reduction tooling

**File:** `scripts/xtf_reduce.ps1`

A PowerShell utility that extracts the original 1024-byte XTF file-header block
plus the first *N* ping/nav packets from a real capture, producing a valid,
sub-1 MB sub-segment suitable for checking into `tests/fixtures/`. A companion
`scripts/xtf_header_probe.ps1` prints header/channel ground truth for picking
reduction parameters and writing accurate assertions.

### 2. FIX-016 — Edgetech 4200.E via Triton Isis (real)

**Fixture:** `tests/fixtures/fix016_edgetech4200_isis_reduced.xtf`
**Test:** `test_fix016_edgetech4200_isis_real`

Reduced from the NBP05-05 survey capture (SonarType=38, SonarName
"Edgetech_4200.E"). Validates real-world dual-frequency 4-channel routing:

- 4 SSS channels — PORT_LOW/STBD_LOW @120 kHz, PORT_HI/STBD_HI @410 kHz
- `metadata.frequency_hz` = 410 kHz (primary), `low_frequency_hz` = 120 kHz
- 8 kept pings × 4 channels → 32 sidescan entries, both bands and both sides present
- header declares `NavUnits=3` (Projected), but the lat/lon fixes fit WGS-84, so
  the reader overrides the CRS to **Geographic** and emits
  `CoordinateSystemOverridden`

### 3. FIX-017 — TST 500 kHz recorder, 32-bit samples (real)

**Fixture:** `tests/fixtures/fix017_tst500k_32bit_reduced.xtf`
**Test:** `test_fix017_tst500k_32bit_real`

Reduced from a 2024 TST 500 kHz capture. The only fixture exercising the 32-bit
sample path:

- 2 SSS channels (port + starboard), both `BytesPerSample=4` (32-bit)
- single 500 kHz band; `metadata.frequency_hz` = 500000, `low_frequency_hz` = 0
- 8 kept pings × 2 channels → 16 sidescan entries, all decode to non-empty pings
- header declares `NavUnits=0` (Geographic), but the fixes are projected metres,
  so the reader overrides the CRS to **Projected** and emits
  `CoordinateSystemOverridden`

Together the two fixtures exercise **both** symmetric branches of the
coordinate-magnitude override policy in `XtfIndex.cpp`.

### 4. Test-harness disk-fixture support

**Files:** `tests/CMakeLists.txt`, `tests/test_xtf_reader.cpp`

Added a `XTF_FIXTURE_DIR` compile definition giving the test executable the
absolute path to `tests/fixtures/`, and a `fixturePath()` helper so tests can
load reduced real binaries from disk (vs. the synthetic in-memory builders used
by FIX-001 … FIX-015).

### 5. Documentation

- `XTF_FIXTURE_CATALOG.md` — added FIX-016 and FIX-017 (new Slice 02D section).
- `XTF_COMPATIBILITY_MATRIX.md` — promoted dual-frequency, 32-bit samples and the
  CRS magnitude-override rows to cite real fixtures; added Edgetech 4200.E and
  TST 500 kHz vendor-family rows with a "Real file" column; rewrote "Known gaps"
  to record that the ≥2 real-vendor bar is now met.

---

## Fixtures and validation

| ID      | What it proves                                                   | Result    |
|---------|------------------------------------------------------------------|-----------|
| FIX-016 | Real Edgetech 4200.E dual-freq 4-channel; NavUnits=3→Geographic  | supported |
| FIX-017 | Real TST 500 kHz, 32-bit samples; NavUnits=0→Projected           | supported |

Full XtfReader suite: **262 assertions passed, 0 failed** (FIX-001 … FIX-017).
Build links cleanly; the pre-existing intermittent `SeabedAutoDetector` failure
is unrelated to this slice.

---

## Decisions confirmed

No new locked decisions. D-05 (unfinished features hidden/disabled), D-07 (stage
discipline) and D-08 (verification floor: build + ctest pass) apply. The
coordinate-magnitude override remains the intended policy: data magnitudes win
over the declared `NavUnits` header flag.

---

## Stage 02 acceptance status

| Acceptance criterion                                          | Status |
|---------------------------------------------------------------|--------|
| Explicit XTF compatibility matrix exists                      | ✅ done |
| Fixture coverage for main XTF families                        | ✅ 17 fixtures (15 synthetic + 2 real) |
| Unsupported variants fail clearly (not silent mis-parse)      | ✅ Slice 02B |
| SSS/SBP/mag verified across variants                          | ✅ incl. 2 real vendor families |
| Bathymetry supported or explicitly partial/unsupported        | ✅ unsupported + diagnostic |
| Compatibility claims based on fixtures, not only inspection   | ✅ incl. real vendor files |

Minimum first-pass bar: ≥10 documented fixtures ✅ (17), ≥8 runnable cases ✅ (17),
≥2 real vendor families ✅ (FIX-016 Edgetech 4200.E, FIX-017 TST 500 kHz).

**Stage 02 acceptance criteria are now met.**

---

## Next steps

Stage 02 is ready for a stage-gate review against
`docs/00_control/STAGE_GATE_CHECKLIST.md`. Further real-vendor families can be
added incrementally using `scripts/xtf_reduce.ps1` as source files arrive, but
no further fixtures are required to close the stage.
