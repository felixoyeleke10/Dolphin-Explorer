# Stage 08 Slice 117 Closure

## Scope

- active stage: Stage 08 — Systemization Hardening
- active slice: S-117 — raster/3D coordinate and UI-state consistency
- primary goal: make raster placement, 3D visibility, and visible UI labels/actions reflect the state and behavior the application actually implements

## What Changed

- Added explicit raster reads for a requested display CRS. Invalid target CRS input is an error rather than a silent native-coordinate fallback; WGS84 helpers delegate to the generic implementation.
- Raster decoding/reprojection now runs through `OperationManager` off the UI thread. The 2D overlay and 3D terrain use the same project display CRS, and four transformed corners establish bounds for rotated as well as north-up rasters.
- Whole-layer visibility and navigation-track visibility are independent in the viewport host and 3D scene. Layer visibility now covers tracks, curtains, drapes, and terrain; the nav toggle hides only the centerline.
- Hidden 3D content is excluded from drawing, hit testing, survey outlines, legends, and the non-empty HUD decision. Visibility chosen before asynchronous terrain/data creation is replayed after the representation exists.
- Implemented the node-graph group Rename action instead of exposing a clickable stub, and renamed the processing node from “Geo-Correction” to “Layback Correction” because it changes the ping navigation fix rather than georeferencing every sample footprint.
- Centralized Line List row/group construction and targeted refresh. Contact/feature/layer tags, tooltips, bold state, modality, group relocation, and removed-last-tag updates now use the same decoration paths as a full rebuild.

## Files Touched

- `src/io/raster/RasterReader.{h,cpp}`
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.Raster.cpp`
- `src/ui/features/map/MapView3D.{h,cpp,Paint.cpp,Overlays.cpp}`
- `src/ui/features/map/MapViewportHost.{h,cpp}`
- `src/ui/features/nodegraph/NodeGraphViewInput.cpp`
- `src/pipeline/nodes/correction/GeoCorrectNode.{h,cpp}`
- `src/ui/shared/panels/LineListPanel.{h,Tree.cpp}`
- `tests/test_raster_io.cpp`

## Tests Or Validation

- Focused `RasterIO` verification passed, including reprojection to `EPSG:3857`, invalid-target rejection, GeoTIFF replacement, and preservation after a rejected rewrite.
- Static UI-path inspection confirms whole-layer and nav-track state use separate caches/APIs and that terrain visibility is applied after an asynchronous load.
- Final serial MSVC/Ninja build passed; CTest passed 23/23, including `PerfBaseline` and `GlSmoke`.

## Gate Status

- gate items completed: 2D/3D raster coordinates share the project display CRS; layer/nav visibility semantics no longer overwrite one another; exposed rename/processing labels are truthful; Line List refresh and rebuild decoration agree.
- gate items still open: interactive viewport/Line List behavior remains appropriate for manual visual smoke coverage after the automated build/test gate.

## Risks / Follow-Ups

- A raster with no declared source CRS is still read in native coordinates by design; the caller must only use such data when those coordinates already match the intended display space.
- Four-corner bounds describe the overlay extent. They do not rectify nonlinear distortion beyond the GDAL warp already applied to the raster pixels.

## What The Next Stage May Assume

- Raster display work is asynchronous and produces both 2D and 3D data in one explicit display CRS.
- “Hide navigation” does not hide the entire 3D layer, while whole-layer visibility covers every representation including terrain.
- Node-graph group Rename is implemented and the layback node's UI name matches its actual correction scope.
