# Stage 08 Slice 106 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-106 — truthful JSF framing and Message Type 80 decoding
- primary goal: stop arbitrary `.jsf` files from probing as valid sonar and align the supported JSF path with EdgeTech's published binary layout

## What Changed

- `JsfReader::open` now validates the first 16-byte JSF message frame and declared body boundary instead of accepting any readable file.
- Probe and index scans now follow framed message boundaries, report bad markers/implausible sizes/truncation, and require at least one structurally decodable sonar message before claiming success.
- Corrected modality/channel semantics: subsystem `0` is sub-bottom; acoustic subsystems `20..99` are sidescan bands; channel `0/1` is port/starboard. Unsupported subsystem numbers are reported instead of guessed.
- Replaced the incorrect synthetic 240-byte C++ field layout with offset-based little-endian accessors matching EdgeTech Message Type 80.
- Corrected timestamp, ping number, validity flags, coordinate units, sample count (including protocol 0xA MSBs), sample interval, chirp frequency, heading, pitch/roll, depth, altitude, layback, and cable-out decoding.
- Added bounded uncompressed sample-format handling for envelope, raw/real, pixel, and complex analytic data. Compressed and unknown encodings fail explicitly.
- Coordinates now distinguish WGS-84 geographic values from declared projected units; missing/mixed navigation is surfaced through probe state and import diagnostics.

Reference used: EdgeTech, *JSF File and Message Descriptions*, document `0023492` (available from the [EdgeTech Resource Center](https://www.edgetech.com/resource-center/)).

## Files Touched

- `src/io/jsf/JsfReader.{h,cpp}`
- `src/io/jsf/JsfReader_p.h`
- `src/io/jsf/JsfReader.Probe.cpp`
- `src/io/jsf/JsfReader.Index.cpp`
- `src/io/jsf/JsfReader.Decode.cpp`
- `tests/test_jsf_reader.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- New `JsfReader` CTest target: 62 checks passed.
- Coverage includes arbitrary junk, a truncated first message, a valid non-sonar-only JSF, a spec-shaped sidescan Message Type 80, a spec-shaped sub-bottom Message Type 80, modality/channel classification, navigation/orientation/scaling, artifact decoding, and explicit compressed-encoding rejection.
- `dolphin-io` and `test_jsf_reader` rebuilt successfully with MSVC/Ninja.

## Gate Status

- gate items completed: JSF probe success now means the file contains a framed, supported, decodable sonar message; the basic uncompressed Message Type 80 path has regression coverage.
- gate items still open: real-vendor JSF fixtures are still needed before widening compatibility claims to compressed data, legacy Message Type 82, or less common subsystems.

## Risks / Follow-Ups

- This slice deliberately narrows support to encodings the reader actually implements. Compressed JSF and legacy Message Type 82 remain unsupported and are reported as such.
- The synthetic fixtures prove the published binary contract but do not replace a checked-in, redistributable real-vendor corpus.

## What The Next Stage May Assume

- Junk or truncated files renamed to `.jsf` do not enter the import workflow as valid sidescan.
- Uncompressed EdgeTech Message Type 80 uses subsystem for modality/frequency band and channel for port/starboard throughout probe, index, and decode.
