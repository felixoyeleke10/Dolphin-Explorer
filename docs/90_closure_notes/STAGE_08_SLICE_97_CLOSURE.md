# Stage 08 — Slice 97 Closure: Display-param round-trip tests

## Goal
S-97: regression net under the DisplayStateManager migration — prove the
serialized display state actually survives save → reopen. Survey found
existing coverage was better than expected (`testDisplayStatePersistence`
already covers visibility + sss/sbp palette + slant-range flag;
`testNavStatePersistence` covers nav params + customized flags), so this slice
fills only the genuine hole: the SBP display/gain/signal param structs.

## Changes
`test_project_storage.cpp` `testDisplayParamsRoundTrip`:
- sets `sbp_display_state` display (gain 2.5, contrast 1.4, invert, bottom
  track off, sound speed 1520), gain chain (static +6 dB, AGC window 128),
  signal chain (envelope, bandpass 500–8000 Hz) plus all three customized
  flags → save → reopen → asserts every field.

## Result
ProjectStorage now 145 checks, all passing; full suite 16/16 green.

Combined with slices 95–96 this closes the first three slices of
`STAGE_08_SYSTEMIZATION_HARDENING.md`. Remaining in the stage: S-98+
(MainWindow decomposition sequence), deliberately unscheduled until asked.
