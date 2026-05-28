# XTF Fixture Catalog

This document is the operating guide for parser and import fixtures.

Its purpose is to stop fixture work from becoming:

- a random pile of files
- undocumented tribal knowledge
- "we think this sample covers that case"

Use this file to define what each fixture proves and why it exists.

## Goals

- make parser coverage intentional
- make compatibility claims testable
- make malformed-file behavior testable
- make regression failures understandable

## Fixture Buckets

Every fixture should belong to one of these buckets.

### 1. Real Reduced Samples

Use real XTF-derived samples reduced to the smallest size that still reproduces the behavior.

Use these for:

- vendor quirks
- real packet layouts
- real nav problems
- real header inconsistencies

### 2. Synthetic Tiny Fixtures

Use tiny generated fixtures when the goal is to isolate one specific parser invariant.

Use these for:

- invalid lengths
- missing sample metadata
- zero-nav behavior
- packet ordering edge cases
- cache/parity edge cases

### 3. Known-Bad Fixtures

Use fixtures that must fail clearly and honestly.

Use these for:

- unsupported packet extensions
- unsupported channel mappings
- corrupt record geometry
- truncated files
- invalid file structure

## Required Metadata For Every Fixture

Every fixture entry should record all of these:

- fixture id
- filename or path
- source bucket
- XTF family or vendor if known
- primary purpose
- expected support state:
  - supported
  - recognized but partial
  - unsupported
- expected modality mix
- expected coordinate behavior
- expected artifact counts if practical
- expected failure mode if unsupported
- notes about sensitivity or licensing

## Suggested Fixture Entry Template

```md
## FIX-XXX

- file:
- bucket:
- vendor/family:
- purpose:
- expected support state:
- expected modalities:
- expected coordinate behavior:
- expected artifact/index counts:
- expected failure mode:
- used by tests:
- notes:
```

## Minimum Starter Set

The first useful fixture set should include at least:

- standard dual-channel sidescan XTF
- split-packet sidescan XTF
- sub-bottom XTF
- magnetometer-carrying XTF
- projected-nav XTF
- zero-ping-nav plus `PACKET_NAV` backfill XTF
- missing or wrong `BytesPerSample`
- missing `NumSamples`
- unusual or weak channel metadata
- truncated record fixture
- oversized/corrupt record-length fixture
- bathymetry-containing XTF
- one vendor-specific sample you care about most

## Test Expectations

Every fixture should be tied to at least one of these:

- parse success expectations
- parse failure expectations
- artifact-count expectations
- modality-classification expectations
- coordinate interpretation expectations
- cache/raw parity expectations

If a fixture is not attached to a test or a documented manual validation step, it is not really part of the fixture program yet.

## Reduction Rule

Prefer reduced fixtures over giant full survey files.

The ideal fixture:

- is as small as possible
- still reproduces the important behavior
- can be checked into the repo if licensing permits
- runs quickly in automated tests

If a real sample is too large or sensitive:

- minimize it
- anonymize it if possible
- or document why it must stay external

## External Fixture Rule

If some fixtures cannot live in the repo:

- record them here anyway
- give them stable ids
- describe where they live
- describe what they prove
- document how to run the related validation

Do not let critical fixture knowledge live only in memory.

## Support-State Rule

Do not use fixture presence alone to imply support.

A fixture only contributes to a compatibility claim if:

- the expected result is documented
- the result is validated
- the current stage closure note references that validation

## Ownership

Stage 02 should grow this catalog.

Stage 01 may start the first parser-fixture harness.

Stage 03 may reuse fixtures for benchmark scenarios.

Stage 04 may reuse fixtures for import/reuse workflow validation.

## Bottom Line

Fixtures are not just test files.

They are the evidence base for claiming:

- XTF compatibility
- import correctness
- cache/raw consistency
- honest failure behavior

---

## Stage 02 Slice 02A — Synthetic Fixtures (FIX-001 through FIX-008)

All fixtures in this slice are generated in-code by `tests/test_xtf_reader.cpp`
using the `XtfBuilder` helper.  No external binary files are required.
Each fixture builds a minimal valid (or deliberately invalid) XTF byte stream,
writes it to a temp file, and reads it back with `XtfReader`.

---

## FIX-001

- file: generated in test_fix001_open_rejects_non_xtf
- bucket: known-bad
- vendor/family: n/a
- purpose: reject files whose FileFormat byte is not 0x7B
- expected support state: unsupported
- expected modalities: none
- expected coordinate behavior: n/a (open() must return false)
- expected artifact/index counts: buildIndex never reached
- expected failure mode: XtfReader::open() returns false
- used by tests: test_fix001_open_rejects_non_xtf (test_xtf_reader.cpp)
- notes: 256-byte file of zeros; FileFormat = 0x00

---

## FIX-002

- file: generated in test_fix002_dual_channel_sss
- bucket: synthetic tiny
- vendor/family: standard dual-channel SSS
- purpose: standard 2-channel sidescan ping round-trip with correct lat/lon
- expected support state: supported
- expected modalities: SSS port + starboard
- expected coordinate behavior: geographic (NavUnits=0), lat=48.0 lon=2.0
- expected artifact/index counts: 2 entries, both Sidescan
- expected failure mode: n/a
- used by tests: test_fix002_dual_channel_sss (test_xtf_reader.cpp)
- notes: TypeOfChannel 1=port 2=stbd; 4 samples each at 16-bit; SlantRange=75m

---

## FIX-003

- file: generated in test_fix003_unknown_channel_skipped
- bucket: known-bad
- vendor/family: n/a
- purpose: prove classifyChannel returns nullopt for out-of-range channel numbers
- expected support state: unsupported (ping channel is skipped)
- expected modalities: none (channel index 5 out of range for 1-channel file)
- expected coordinate behavior: n/a
- expected artifact/index counts: 0 entries
- expected failure mode: channel silently skipped; no crash; empty index
- used by tests: test_fix003_unknown_channel_skipped (test_xtf_reader.cpp)
- notes: regression test for classifyChannel out-of-range fix; before the fix this
  returned 1 spurious Sidescan entry

---

## FIX-004

- file: generated in test_fix004_nav_backfill
- bucket: synthetic tiny
- vendor/family: zero-ping-nav plus PACKET_NAV
- purpose: pings with SensorY/X = 0 are backfilled from PACKET_NAV fixes
- expected support state: supported
- expected modalities: SSS port
- expected coordinate behavior: geographic; ping zero-nav backfilled to lat=55.5 lon=-3.0
- expected artifact/index counts: 1 entry, Sidescan
- expected failure mode: n/a
- used by tests: test_fix004_nav_backfill (test_xtf_reader.cpp)
- notes: PACKET_NAV at T=0s; ping at T+1s; last-fix extrapolation path

---

## FIX-005

- file: generated in test_fix005_projected_nav_units
- bucket: synthetic tiny
- vendor/family: projected-nav (NavUnits=3)
- purpose: NavUnits=3 sets coordinate_ref to Projected kind
- expected support state: supported (recognized but unprojected)
- expected modalities: SSS port
- expected coordinate behavior: projected; NavUnits=3 confirmed by large coordinate magnitudes
- expected artifact/index counts: 1 entry, Sidescan; entry.is_projected=true
- expected failure mode: n/a
- used by tests: test_fix005_projected_nav_units (test_xtf_reader.cpp)
- notes: coordinates are UTM-style (500000, 200000); magnitude validation agrees with header

---

## FIX-006

- file: generated in test_fix006_bps_inference
- bucket: synthetic tiny
- vendor/family: missing BytesPerSample
- purpose: BytesPerSample=0 in channel header is inferred from record geometry
- expected support state: supported
- expected modalities: SSS starboard (no reversal, simpler to verify sample order)
- expected coordinate behavior: geographic
- expected artifact/index counts: 1 entry, Sidescan; 4 samples decoded correctly
- expected failure mode: n/a
- used by tests: test_fix006_bps_inference (test_xtf_reader.cpp)
- notes: record = 256+64+8=328 bytes; inferred bps = (328-320)/4 = 2; samples[0]=1000

---

## FIX-007

- file: generated in test_fix007_truncated_file
- bucket: known-bad
- vendor/family: truncated record
- purpose: second record whose NumBytesThisRecord points past EOF is rejected gracefully
- expected support state: unsupported (truncated record is skipped)
- expected modalities: SSS port (first ping only)
- expected coordinate behavior: geographic
- expected artifact/index counts: 1 entry from the complete first ping; truncated ping dropped
- expected failure mode: loop exits cleanly; no crash; no spurious entries
- used by tests: test_fix007_truncated_file (test_xtf_reader.cpp)
- notes: second PACKET_PING header present but channel data absent; guard is
  `if (offset + record_bytes > m_fileSize) break`

---

## FIX-008

- file: generated in test_fix008_subbottom_channel
- bucket: synthetic tiny
- vendor/family: sub-bottom (TypeOfChannel=0)
- purpose: channel with TypeOfChannel=0 produces SubBottom ArtifactType entries
- expected support state: supported
- expected modalities: sub-bottom trace
- expected coordinate behavior: geographic
- expected artifact/index counts: 1 entry, SubBottom; 4 normalised float samples
- expected failure mode: n/a
- used by tests: test_fix008_subbottom_channel (test_xtf_reader.cpp)
- notes: normalization: raw uint16_t = 32768 → 0.0; raw 1000 → (1000-32768)/32768 ≈ -0.9695
