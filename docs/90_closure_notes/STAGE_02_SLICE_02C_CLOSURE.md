# Stage 02 — Slice 02C Closure Note

**Slice:** 02C — Split-packet & dual-frequency routing + compatibility matrix
**Closed:** 2026-06-03
**Status:** Complete

---

## What this slice delivered

### 1. Split-packet sidescan validation

**Fixture:** FIX-014 (`test_fix014_split_packet_sidescan`)

Proves that port and starboard channels arriving as two separate `PACKET_PING`
records (each `NumChansToFollow=1`) are both indexed and classified correctly as
Port and Starboard. This layout was handled by existing code but had no test;
"split-packet sidescan" is in the stage plan's required starter set.

### 2. Dual-frequency (Edgetech 4200-style) routing validation

**Fixture:** FIX-015 (`test_fix015_dual_frequency_routing`)

Proves the `SubChannelNumber` routing path used in both `XtfIndex.cpp` and
`XtfPayload.cpp`:

```
chan_info_index = ChannelNumber + SubChannelNumber * NumChansToFollow
```

A 4-channel file (LF port/stbd at index 0/1, HF port/stbd at index 2/3) is read
with two ping packets reusing ChannelNumber 0/1; the HF packet sets
`SubChannelNumber=1`. The reader routes each ping to the correct channel-info
band, so:

- `metadata.frequency_hz` = 400 kHz (primary/highest), `low_frequency_hz` = 100 kHz
- two index entries carry 100 kHz, two carry 400 kHz
- HF entries round-trip through `readArtifact` with the correct frequency and
  port/starboard classification

No production code changes were required — this slice converts inferred
behaviour into fixture-proven behaviour.

### 3. XTF compatibility matrix (Stage 02 deliverable #1)

**File:** `docs/20_guides/XTF_COMPATIBILITY_MATRIX.md`

The checked-in compatibility matrix required by the stage plan. It records:

- a capability matrix (per parser feature) with support state and fixture id
- a vendor/recorder family matrix for the layout styles handled
- an explicit "Known gaps" section

A capability is only marked **supported** when a runnable fixture proves it;
code-only behaviour is marked **partial**.

---

## Fixtures and validation

| ID      | What it proves                                       | Result      |
|---------|------------------------------------------------------|-------------|
| FIX-014 | Split-packet SSS (1 channel/ping) → 2 entries        | supported   |
| FIX-015 | Dual-frequency SubChannelNumber routing → LF+HF bands| supported   |

Full XtfReader suite: **97 assertions passed, 0 failed** (FIX-001 … FIX-015).

---

## Honest gaps (per stage plan §285)

This slice does **not** satisfy the stage plan's "≥2 prioritized real vendor or
recorder families" bar with real files:

- All fixtures FIX-001 … FIX-015 are **synthetic** `XtfBuilder` byte streams.
  They reproduce the relevant *layouts* (including the Edgetech 4200-style
  dual-frequency routing pattern) but are **not** reduced real-world captures.
- Acquiring and reducing real vendor binaries requires source files from the
  team. Until then, the compatibility matrix marks vendor-family rows as
  validated by synthetic layout fixtures, not certified against real captures.
- 32-bit sample decode and attitude/notes packet *use* remain partial.

These gaps are recorded in `XTF_COMPATIBILITY_MATRIX.md` → "Known gaps".

---

## Decisions confirmed

No new locked decisions. D-07 (stage discipline) and D-08 (verification floor:
build + ctest pass) apply.

---

## Stage 02 acceptance status

| Acceptance criterion                                          | Status |
|---------------------------------------------------------------|--------|
| Explicit XTF compatibility matrix exists                      | ✅ done |
| Fixture coverage for main XTF families                        | ✅ synthetic (10+ fixtures) |
| Unsupported variants fail clearly (not silent mis-parse)      | ✅ Slice 02B |
| SSS/SBP/mag verified across variants                          | ◐ synthetic only |
| Bathymetry supported or explicitly partial/unsupported        | ✅ unsupported + diagnostic |
| Compatibility claims based on fixtures, not only inspection   | ◐ synthetic fixtures; real vendor files pending |

Minimum first-pass bar: ≥10 documented fixtures ✅ (15), ≥8 runnable cases ✅ (15),
≥2 real vendor families ❌ (blocked on source files — flagged above).

---

## Next slice

Slice 02D (when real files are available): reduce ≥2 real vendor/recorder XTF
files into checked-in fixtures and validate them against the matrix. Optionally
add a 32-bit sample fixture to promote that row from partial to supported.
