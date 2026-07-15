# Stage 08 Slice 133 Closure — Waterfall inspector metadata containment

## What shipped

- Allowed Waterfall inspector metadata value labels to shrink to their assigned
  value column instead of imposing a text-width minimum on the scroll content.
- Kept long values inside the fixed inspector by wrapping them as plain text.
- Prevented survey/model/frequency metadata from extending beneath the sidescan
  waterfall at narrow window sizes and display scale factors.

## What was tested

- Incremental `dolphin-ui` build.
- Existing automated tests were not expanded because Stage 08 explicitly
  defers QTest-driven widget layout tests; this is a Qt layout-policy correction.

## What remains

- No broader Waterfall layout or metadata presentation changes are included in
  this slice.
