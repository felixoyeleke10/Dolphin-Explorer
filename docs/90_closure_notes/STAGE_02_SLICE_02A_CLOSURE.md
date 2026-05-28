# Stage 02 — Slice 02A Closure Note

**Slice:** 02A — Foundation: classifyChannel fix + XTF fixture harness  
**Closed:** 2026-05-07  
**Status:** Complete

---

## What this slice delivered

### 1. Bug fix — `classifyChannel` out-of-range behaviour

**File:** `src/io/xtf/XtfReader.cpp` line 238

Before this fix, `classifyChannel()` returned `core::ArtifactType::Sidescan` when
`chan_number >= m_chan_info.size()`.  This caused `buildIndex()` to emit a spurious
Sidescan entry for every ping channel whose channel number was not declared in the
file header — silently misclassifying unknown data rather than skipping it.

After the fix the function returns `std::nullopt` for out-of-range indices.
`buildIndex()` already skips entries when `classifyChannel` returns `nullopt`, so
the change requires no other callers to be updated.

Validated by FIX-003.

---

### 2. XTF test harness — `tests/test_xtf_reader.cpp`

Eight synthetic fixture tests wired into CTest as `XtfReader`.  Each test builds a
minimal XTF byte stream using an in-code `XtfBuilder`, writes it to a temp file,
reads it back with `XtfReader`, and asserts the expected outcome.

No external binary files are required.  Every fixture is reproducible from source.

---

### 3. Fixture catalog — `docs/20_guides/XTF_FIXTURE_CATALOG.md`

FIX-001 through FIX-008 documented with bucket, purpose, support state, expected
counts, and test reference.

---

## Fixtures and validation

| ID      | What it proves                                  | Result      |
|---------|-------------------------------------------------|-------------|
| FIX-001 | `open()` rejects wrong magic byte               | skips/false |
| FIX-002 | Dual-channel SSS → 2 entries, correct lat/lon   | supported   |
| FIX-003 | Out-of-range channel number → skipped           | skipped     |
| FIX-004 | PACKET_NAV backfill for zero-nav pings          | supported   |
| FIX-005 | NavUnits=3 → coordinate_ref is Projected        | supported   |
| FIX-006 | BytesPerSample=0 inferred from record geometry  | supported   |
| FIX-007 | Truncated record → partial index, no crash      | graceful    |
| FIX-008 | TypeOfChannel=0 → SubBottom ArtifactType        | supported   |

Minimum bar from stage plan: ≥8 runnable cases. Delivered: 8.

---

## What is NOT covered by this slice

- Real reduced samples from known vendors (Edgetech, Klein, C-MAX, etc.)
- Dual-frequency files (Edgetech 4200-style SubChannelNumber routing)
- Bathymetry channel types (TypeOfChannel 3/4)
- XTF files with attitude/notes packets
- Missing or inconsistent NumSamples vs SamplesPerChannel edge cases
- Cache/raw parity for XTF artifacts (covered by ParsedCache tests already)

These are candidates for Slice 02B and later.

---

## Decisions confirmed

No new locked decisions were needed.  D-07 (stage discipline) and D-08
(verification floor: build + ctest pass) apply.

---

## Next slice

Slice 02B: real vendor fixtures and dual-frequency channel routing.
