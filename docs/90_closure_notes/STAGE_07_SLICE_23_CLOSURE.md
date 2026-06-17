# Stage 07 Slice 23 — "Only 1 of 6 lines visible" on open: survey framing

## Diagnosis (from dolphin_debug.log, not guesswork)
The `[nav]` + `[map] combined` logs proved the data was correct all along:
```
[map] combined: 4 layer(s) visible, 3 with track, 3243 nav pts; bbox lon[-16.130..-16.118] lat[57.309..57.316]
[nav] layer_..._7: 1097 pts  lon[-16.123..-16.118] lat[57.313..57.316]
```
All 4 loaded lines (active raster + 3 nav tracks) were built, visible-flagged, and in
the combined render with sane, correct coordinates. The combined bbox was right. So it
was never a data / reproject / paint bug — it was **viewport framing**.

The active line's extent is `lon[-16.130..-16.124]`; line `_7` lives at
`lon[-16.123..-16.118]`, entirely outside it. The view was fit to the active line, so the
other lines fell off the right edge.

## Root cause
`MapView::setLayerMapData` only re-fits when `!m_user_interacted`. But
`setZoomFromMpp` (a *programmatic* viewport sync — status-bar scale echo / 2D↔3D
switch, `MapViewportHost::setViewportScale`) sets `m_user_interacted = true`. So a
sync during open silently suppressed the auto-fit, freezing the view on the active
line. Genuine gestures (wheel/drag/zoom/pan) set the flag too, but so did this
programmatic path — the flag conflated the two.

## Fix
A one-shot **frame-survey** flag, independent of the interaction flag:
- `MapView::requestFrameSurvey()` sets `m_frame_survey_pending`.
- `setLayerMapData` auto-fits when `m_frame_survey_pending || !m_user_interacted` — so
  during open every arriving line (active mosaic, each nav track, late reindex tracks)
  re-fits to the **combined** extent, regardless of a stale programmatic interaction flag.
- The flag is cleared by any **genuine** gesture (zoomAtPoint, drag-pan, wheelEvent,
  panByPixels, fitToLayer) — NOT by the programmatic `setZoomFromMpp`. So once the user
  takes control, background arrivals stop yanking the view.
- `MainWindow::firstLayerReady` calls `requestFrameSurvey()` at the start of open.

## Result
Opening a project frames the whole survey: the active line's mosaic + every other
line's nav track all visible; the view expands as the 2 footerless lines finish
reindexing. User pan/zoom then sticks.

## Build / tests
Full build green, exe relinked. Runtime check (user): open the project — all lines
should be in view, not just the active one. The `[nav]`/`[map]`/`[raster]`/`[timing]`
diagnostics remain for this round; strip once open speed + framing are confirmed.
