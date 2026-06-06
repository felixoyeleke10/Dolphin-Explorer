# XTF Compatibility Matrix

This is the checked-in compatibility matrix required by Stage 02
(`STAGE_02_XTF_COMPATIBILITY_EXPANSION.md`, deliverable #1 / workstream #1).

It records what the XTF reader actually does, separated into three support
states, and ties each claim to fixture validation where one exists. It is
deliberately conservative: a capability is only listed as **supported** when a
runnable fixture proves it. Anything proven only by code inspection is marked
**partial** until a fixture exists.

## Support states

- **supported** — decoded correctly and validated by at least one fixture.
- **partial** — recognized and handled safely (no mis-parse), but either not
  fully decoded or not yet fixture-proven.
- **unsupported** — not decoded; reader reports it via a diagnostic and skips it.

---

## Capability matrix (by parser feature)

| Capability                              | State       | Fixture(s)        | Notes |
|-----------------------------------------|-------------|-------------------|-------|
| Dual-channel sidescan (one ping, 2 ch)  | supported   | FIX-002           | TypeOfChannel 1/2 |
| Split-packet sidescan (1 ch per ping)   | supported   | FIX-014           | separate PACKET_PING per channel |
| Dual-frequency sidescan (LF+HF)         | supported   | FIX-015, FIX-016  | Edgetech 4200-style SubChannelNumber routing; FIX-016 real file |
| Sub-bottom trace (TypeOfChannel=0)      | supported   | FIX-008           | normalised float samples |
| Magnetometer (packet Mag fields)        | supported   | (header decode)   | extracted from PACKET_PING header |
| PACKET_NAV backfill / interpolation     | supported   | FIX-004, FIX-011  | linear interp + extrapolation |
| Projected nav (NavUnits=3)              | supported   | FIX-005           | Pseudo-projected SpatialRef |
| Geographic nav (NavUnits=0/1)           | supported   | FIX-002, FIX-004  | WGS-84 |
| CRS magnitude override (geo↔projected)  | supported   | FIX-016, FIX-017  | data magnitudes win over header; both override directions on real files |
| 8/16-bit samples                        | supported   | FIX-002, FIX-008  | BytesPerSample 1/2 |
| 32-bit samples (BytesPerSample=4)       | supported   | FIX-017           | real TST 500 kHz capture |
| BytesPerSample=0 inference              | supported   | FIX-006, FIX-010  | from record geometry |
| Missing NumSamples → SamplesPerChannel  | supported   | (XtfIndex/Payload)| header fallback |
| Truncated record                        | supported   | FIX-007           | graceful partial index |
| Oversize/corrupt record length          | supported   | (XtfIndex guard)  | ImplausibleRecordSize, skipped |
| Bad packet magic / resync               | supported   | FIX-009           | ResyncedPacket diagnostic |
| Out-of-range channel number             | supported   | FIX-003           | skipped, no spurious entry |
| Bathymetry channel (TypeOfChannel=3/4)  | unsupported | FIX-012           | UnsupportedChannelType diagnostic |
| Unknown channel type                    | unsupported | FIX-012 path      | UnsupportedChannelType diagnostic |
| Attitude packet (HeaderType=3)          | partial     | FIX-013 path      | recognized, Info diagnostic, not indexed |
| Notes packet (HeaderType=6)             | partial     | FIX-013 path      | recognized, Info diagnostic, not indexed |
| Unknown packet type                     | unsupported | FIX-013           | UnsupportedPacketType diagnostic |

---

## Vendor / recorder family matrix

These rows describe the layout styles the reader is designed to handle. Rows
citing a **reduced real capture** (FIX-016, FIX-017) are validated against true
vendor binaries; the remaining rows are validated by **synthetic** fixtures that
reproduce the layout. Vendor names indicate the layout family, not a certified
broad compatibility claim across all firmware revisions.

| Family / layout style          | SSS | SBP | Bathy | Sample width | Nav units | Packet style        | State    | Real file |
|--------------------------------|-----|-----|-------|--------------|-----------|---------------------|----------|-----------|
| Generic dual-channel SSS       | yes | —   | —     | 8/16-bit     | geo/proj  | dual-channel ping   | supported| synthetic |
| Generic split-packet SSS       | yes | —   | —     | 8/16-bit     | geo/proj  | one channel/ping    | supported| synthetic |
| Edgetech 4200.E (via Isis)     | yes | —   | —     | 16-bit       | proj→geo  | SubChannelNumber    | supported| FIX-016   |
| TST 500 kHz recorder           | yes | —   | —     | 32-bit       | geo→proj  | dual-channel ping   | supported| FIX-017   |
| Generic sub-bottom (CHIRP-like)| —   | yes | —     | 16-bit       | geo       | single-channel ping | supported| synthetic |
| Magnetometer-in-XTF            | opt | —   | —     | n/a          | geo       | mag fields in ping  | supported| synthetic |
| Bathymetry / multibeam XTF     | —   | —   | yes   | n/a          | any       | bathy channels      | unsupported (recognized) | — |

---

## Known gaps (must stay honest per stage plan §285)

The Stage 02 plan requires that missing real-vendor coverage be stated
explicitly rather than implied as broad support.

- **Two real vendor families are now validated** with reduced real captures
  (Slice 02D): FIX-016 (Edgetech 4200.E via Triton Isis) and FIX-017 (TST
  500 kHz recorder). The stage plan's ≥2 real vendor/recorder-family minimum bar
  is therefore satisfied.
- The remaining fixtures (FIX-001 … FIX-015) are synthetic byte streams generated
  by `XtfBuilder` in `tests/test_xtf_reader.cpp`. They prove the parser's handling
  of each *layout* but are not reduced real-world captures.
- Validation is limited to the firmware/recorder revisions in the two reduced
  captures; broader per-vendor certification still requires additional source
  files from the team.
- Bathymetry/multibeam decode is intentionally out of scope; it is recognized
  and reported as unsupported, not decoded.

---

## How to extend this matrix

1. Add the fixture (synthetic in `tests/test_xtf_reader.cpp`, or a reduced real
   file referenced per the External Fixture Rule in `XTF_FIXTURE_CATALOG.md`).
2. Document it in `XTF_FIXTURE_CATALOG.md` with its expected support state.
3. Add or update the row here and cite the fixture id.
4. Reference the validation in the relevant stage closure note.

A row may only claim **supported** once steps 1–4 are complete.
