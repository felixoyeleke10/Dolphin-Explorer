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

---

## Stage 02 Slice 02B — Unsupported-Format Detection (FIX-012, FIX-013)

Slice 02B makes the parser report support state instead of silently dropping
data.  Recognized-but-unsupported channels (bathymetry) and unknown/unsupported
packet types now produce diagnostics during `buildIndex` so the UI can explain
why an expected modality is missing.  Both fixtures are generated in-code by
`tests/test_xtf_reader.cpp`.

---

## FIX-012

- file: generated in test_fix012_bathymetry_channel_unsupported
- bucket: known-bad
- vendor/family: bathymetry-containing XTF
- purpose: a declared bathymetry channel (TypeOfChannel=3) is reported, not silently dropped
- expected support state: recognized but partial (bathymetry not indexed; SSS still indexed)
- expected modalities: SSS port (indexed) + bathymetry (skipped)
- expected coordinate behavior: geographic (NavUnits=0)
- expected artifact/index counts: 1 entry (Sidescan); bathymetry channel produces no entry
- expected failure mode: UnsupportedChannelType diagnostic (Warning); channel skipped
- used by tests: test_fix012_bathymetry_channel_unsupported (test_xtf_reader.cpp)
- notes: file header declares num_sss=1, num_bathy=1; diagnostic is emitted from the
  channel-info declaration scan before any ping is processed

---

## FIX-013

- file: generated in test_fix013_unknown_packet_type
- bucket: known-bad
- vendor/family: n/a (synthetic unknown packet)
- purpose: an unrecognized packet HeaderType is reported and does not derail indexing
- expected support state: recognized but partial (unknown packet skipped; pings kept)
- expected modalities: SSS port (two pings)
- expected coordinate behavior: geographic
- expected artifact/index counts: 2 entries (Sidescan), one per surrounding ping
- expected failure mode: UnsupportedPacketType diagnostic (Warning); record skipped
- used by tests: test_fix013_unknown_packet_type (test_xtf_reader.cpp)
- notes: junk packet HeaderType=99 sits between two valid pings; each distinct
  unsupported packet type is reported only once per buildIndex

---

## Stage 02 Slice 02C — Split-Packet & Dual-Frequency Routing (FIX-014, FIX-015)

Slice 02C proves the `SubChannelNumber` channel-routing paths in
`XtfIndex.cpp` and `XtfPayload.cpp` with synthetic fixtures. These layouts were
already implemented but previously unverified by any test. Both fixtures are
generated in-code by `tests/test_xtf_reader.cpp`.

---

## FIX-014

- file: generated in test_fix014_split_packet_sidescan
- bucket: synthetic tiny
- vendor/family: split-packet sidescan (one channel per PACKET_PING)
- purpose: port and starboard arriving in separate ping packets are both indexed
- expected support state: supported
- expected modalities: SSS port + starboard
- expected coordinate behavior: geographic (NavUnits=0), lat=48.0 lon=2.0
- expected artifact/index counts: 2 entries (one Port, one Starboard)
- expected failure mode: n/a
- used by tests: test_fix014_split_packet_sidescan (test_xtf_reader.cpp)
- notes: contrast with FIX-002 (both channels in one ping); each packet has
  NumChansToFollow=1

---

## FIX-015

- file: generated in test_fix015_dual_frequency_routing
- bucket: synthetic tiny
- vendor/family: Edgetech 4200-style dual-frequency sidescan
- purpose: SubChannelNumber routes pings to the correct channel-info band (LF/HF)
- expected support state: supported
- expected modalities: SSS port + starboard, two frequency bands (100/400 kHz)
- expected coordinate behavior: geographic
- expected artifact/index counts: 4 entries; 2 at 100 kHz, 2 at 400 kHz
- expected failure mode: n/a
- used by tests: test_fix015_dual_frequency_routing (test_xtf_reader.cpp)
- notes: 4 channel-info entries (LF port/stbd index 0/1, HF port/stbd index 2/3);
  HF packet sets XtfPacketHeader::SubChannelNumber=1 →
  chan-info index = ChannelNumber + SubChannelNumber * NumChansToFollow;
  metadata.frequency_hz=400 kHz (primary), low_frequency_hz=100 kHz

---

## Stage 02 Slice 02D — Real-Vendor Reduced Fixtures (FIX-016, FIX-017)

Slice 02D meets the stage's "≥2 real vendor/recorder families" bar.  Unlike
FIX-001…FIX-015 (synthetic `XtfBuilder` byte streams), these are **reduced
sub-segments of real-world vendor captures**: the original 1024-byte file header
block plus the first 8 ping/nav packets, extracted by `scripts/xtf_reduce.ps1`.
The reduced binaries are checked into `tests/fixtures/` and loaded from disk via
the `XTF_FIXTURE_DIR` compile definition (see `tests/CMakeLists.txt`).  Both
files independently exercise the symmetric coordinate-magnitude override policy.

---

## FIX-016

- file: tests/fixtures/fix016_edgetech4200_isis_reduced.xtf (reduced real capture)
- bucket: real vendor reduced
- vendor/family: Edgetech 4200.E recorded via Triton Isis (NBP05-05 survey)
- purpose: validate real dual-frequency 4-channel sidescan routing against a true vendor file
- expected support state: supported (real-file validated)
- expected modalities: SSS port + starboard at two bands (PORT_LOW/STBD_LOW @120 kHz, PORT_HI/STBD_HI @410 kHz)
- expected coordinate behavior: header NavUnits=3 (Projected), but lat/lon fixes fit WGS-84 → overridden to Geographic
- expected artifact/index counts: 32 entries (8 kept pings × 4 channels), all Sidescan, all is_projected=false
- expected failure mode: n/a; CoordinateSystemOverridden diagnostic (Warning) emitted
- used by tests: test_fix016_edgetech4200_isis_real (test_xtf_reader.cpp)
- notes: SonarType=38, SonarName "Edgetech_4200.E"; metadata.frequency_hz=410 kHz (primary),
  low_frequency_hz=120 kHz; 16-bit samples; reduced via scripts/xtf_reduce.ps1 -Pings 8

---

## FIX-017

- file: tests/fixtures/fix017_tst500k_32bit_reduced.xtf (reduced real capture)
- bucket: real vendor reduced
- vendor/family: TST 2024 500 kHz recorder
- purpose: validate the 32-bit sample path (BytesPerSample=4) against a true vendor file
- expected support state: supported (real-file validated; promotes 32-bit from partial)
- expected modalities: SSS port + starboard, single 500 kHz band
- expected coordinate behavior: header NavUnits=0 (Geographic), but fixes are projected metres → overridden to Projected
- expected artifact/index counts: 16 entries (8 kept pings × 2 channels), all Sidescan, all is_projected=true
- expected failure mode: n/a; CoordinateSystemOverridden diagnostic (Warning) emitted
- used by tests: test_fix017_tst500k_32bit_real (test_xtf_reader.cpp)
- notes: chan[0]=port chan[1]=stbd, both bps=4 (32-bit); metadata.frequency_hz=500000,
  low_frequency_hz=0; only fixture exercising the 32-bit decode path; reduced via
  scripts/xtf_reduce.ps1 -Pings 8
