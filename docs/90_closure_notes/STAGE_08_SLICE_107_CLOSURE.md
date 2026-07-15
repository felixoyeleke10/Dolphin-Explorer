# Stage 08 Slice 107 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-107 — truthful import surface and modality filters
- primary goal: remove redundant/unsupported format promises from the shipped Import menu and keep modality filters synchronized with actual probe/decode support

## What Changed

- Collapsed three identical sidescan launch paths into one truthful `XTF / JSF / DLPD…` action.
- Corrected sub-bottom import to expose every implemented source family: `XTF / JSF / SEG-Y / DLPD…`.
- Replaced the unsupported magnetometer `CSV / Text…` claim with `XTF / DLPD…` and taught the XTF probe to report magnetometer data when ping headers contain magnetic samples.
- Removed enabled Humminbird SON, Lowrance SL2/SL3, Kongsberg ALL/KMALL, and Reson S7K actions; no matching probe or reader exists.
- Hid the Bathymetry submenu until a decodable multibeam reader exists.
- DLPD probing no longer exposes multibeam records as selectable when `ParsedCacheReader` cannot decode their payload; multibeam-only caches now fail with a specific unsupported message.
- Central sub-bottom file filters now include XTF and JSF instead of silently excluding implemented readers.

## Files Touched

- `src/ui/mainwindow/MainWindow.Menus.cpp`
- `src/io/ProbeDispatch.cpp`
- `src/io/xtf/XtfReader.Probe.cpp`
- `tests/test_jsf_reader.cpp`
- `tests/CMakeLists.txt`

## Tests Or Validation

- `JsfReader`: 72 checks passed, including exact supported-filter assertions.
- `XtfReader`: 262 checks passed after XTF magnetometer probe detection changed.
- `dolphin-ui-mainwindow` rebuilt successfully with MSVC/Ninja.
- Repository search is clean for the removed unsupported menu labels.

## Gate Status

- gate items completed: the enabled import surface now names only formats that route to an implemented reader for that modality; duplicate format actions no longer open the same indistinguishable dialog.
- gate items still open: multibeam, Humminbird, Lowrance, KMALL/ALL, S7K, and magnetometer CSV require real readers plus fixtures before they can return to the menu.

## Risks / Follow-Ups

- XTF magnetometer detection is based on non-zero `MagX/MagY/MagZ`, matching the existing XTF index/decode contract. A redistributable magnetometer-bearing XTF fixture would strengthen this path further.

## What The Next Stage May Assume

- Every enabled modality-specific Import entry is backed by the central `fileFilterForArtifactType` list and an implemented probe/decode path.
- Multibeam is not presented as importable until its DLPD or vendor-format payload can actually be decoded.
