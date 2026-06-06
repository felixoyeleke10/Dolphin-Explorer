# Stage 02 — Slice 02B Closure Note

**Slice:** 02B — Unsupported-format detection + support-state reporting
**Closed:** 2026-06-03
**Status:** Complete

---

## What this slice delivered

### 1. Channel-type support reporting

**File:** `src/io/xtf/XtfIndex.cpp` (`buildIndex`)

Every channel declared in the file header is now classified at the start of
`buildIndex`:

- **Supported** — sub-bottom (0), port SSS (1), starboard SSS (2): indexed as before.
- **Recognized but unsupported** — bathymetry (3/4): emits a Warning
  `UnsupportedChannelType` diagnostic and is skipped.
- **Unrecognized** — any other `TypeOfChannel`: emits a Warning
  `UnsupportedChannelType` diagnostic and is skipped.

The diagnostic names the channel index, channel name, and numeric type so the
UI can explain why an expected modality (e.g. bathymetry) is missing instead of
the data silently vanishing. `classifyChannel` is unchanged — it still returns
`std::nullopt` for non-SSS/SBP types; the new diagnostics come from a single
channel-info declaration scan, so there is no per-ping diagnostic spam.

### 2. Packet-type support reporting

**File:** `src/io/xtf/XtfIndex.cpp` (`buildIndex` main loop)

Packet types other than `PACKET_PING` and `PACKET_NAV` now emit an
`UnsupportedPacketType` diagnostic:

- **Recognized but unsupported** — attitude (3), notes (6): `Info` severity.
- **Unrecognized** — any other `HeaderType`: `Warning` severity, record skipped.

Each distinct `HeaderType` is reported only once per `buildIndex`
(`reported_pkt_type[256]` guard). A `PACKET_PING` carrying zero channels still
falls through silently — it has no sample data to index and is not an error.

### 3. Channel-type constants

**File:** `src/io/xtf/XtfReader_p.h`

Added `CHAN_BATHY = 3` and `CHAN_BATHY_ALT = 4` alongside the existing
`CHAN_SUBBOT` / `CHAN_PORT_SSS` / `CHAN_STBD_SSS` constants.

### 4. Fixtures

**File:** `tests/test_xtf_reader.cpp`

Two new in-code fixtures, plus a generic `XtfBuilder::writeSimplePacket` helper
for emitting arbitrary-typed packets. Wired into the existing `XtfReader` CTest.

---

## Fixtures and validation

| ID      | What it proves                                            | Result      |
|---------|-----------------------------------------------------------|-------------|
| FIX-012 | Bathymetry channel (type 3) → UnsupportedChannelType      | skipped+diag|
| FIX-013 | Unknown packet type → UnsupportedPacketType, pings kept   | skipped+diag|

Full XtfReader suite: **75 assertions passed, 0 failed** (FIX-001 … FIX-013).

---

## What is NOT covered by this slice

- Actual decoding of bathymetry channels (still unsupported).
- Decoding/use of attitude or notes packets (recognized, not indexed).
- Real vendor fixtures and dual-frequency channel routing (carried forward).
- Surfacing these diagnostics in the UI Problems panel beyond what the existing
  diagnostic pipeline already provides.

---

## Decisions confirmed

No new locked decisions were needed. D-07 (stage discipline) and D-08
(verification floor: build + ctest pass) apply. The shipped-surface policy
(D-05) is reinforced: unsupported data now reports its state rather than being
silently dropped.

---

## Next slice

Slice 02C: real vendor reduced fixtures and dual-frequency (Edgetech 4200-style)
channel routing.
