# Stage 07 · Slice 86 — 3D toggle back in the viewport corner

## Goal (user direction)
Slice edf9448's native-GL blink fix moved the 2D/3D toggle (+ Terrain button)
out of the viewport corner into a toolbar strip below the map. The user wants
the button back in the bottom-right corner over the map.

## Why it could not simply float again
The 3D view is a native QOpenGLWindow embedded via createWindowContainer.
Qt documents the limitation: the embedded window "stacks on top of the widget
hierarchy as an opaque box". Verified empirically this session — even a
`WA_NativeWindow` sibling with `raise()` is forced under the container's GL
window. NOTHING widget-based can float over the 3D viewport.

## Solution — split by mode
- **2D mode**: the "3D" button floats bottom-right over the map again as a
  plain child widget (`#mapViewportOverlay`, positioned in
  `MapViewportHost::positionOverlay`). The 2D MapView is an ordinary
  raster-painted widget, so overlaying it is safe. Hidden while in 3D.
- **3D mode**: `MapView3D::drawViewButtons` paints "⊞ Terrain" and "2D"
  chips into the HUD (same QPainter pass as the compass rose / FPS badge),
  same corner, dark badge styling. `mousePressEvent` hit-tests the chip rects
  ahead of camera drags; hover shows a pointing-hand cursor. New signal
  `switchTo2DRequested` → host `setMode3D(false)`.
- **Terrain load feedback** moved in-scene too: `m_terrain_loading` drives a
  "Loading terrain…" HUD chip (set in `loadTerrainFile`, cleared in
  `applyTerrainResult`). The widget-based Terrain button and status label are
  deleted from MapViewportHost (members, wiring, and their QSS in
  AppStyleShell + AppStyleDialogs).
- The below-map toolbar strip is gone — the map canvas regains that row.

## Verification
- Build green; 16/16 tests (incl. GlSmoke).
- 2D: verified visually via in-app widget grab — "3D" button bottom-right
  over the chart, old look restored.
- 3D: mode-switch round trip verified via instrumented trace
  (setMode3D → modeChanged, stable in 3D, no crash with drawViewButtons in
  the paint loop). Pixel-verification of the chips was blocked by the test
  VM (1280×720 display smaller than the app's minimum layout + GL container
  resize artifacts + grabFramebuffer returning black on the virtual GPU
  driver) — needs a one-click confirmation on a real display.
