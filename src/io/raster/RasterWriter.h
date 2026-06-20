#pragma once
// RasterWriter — GeoTIFF export via GDAL (symmetric with RasterReader).
//
//   * Elevation/depth grid (core::RasterGrid)  → single-band Float32 GeoTIFF
//   * Visual RGBA image                         → 8-bit RGB(A) GeoTIFF
//
// Qt-free (io layer). DEFLATE-compressed + tiled output with the grid's geo-
// transform and CRS (OGC WKT) written as GeoTIFF geo-keys, so the result reopens
// in QGIS/ArcGIS/SonarWiz with correct georeferencing.
#include "core/RasterGrid.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dolphin::io {

// Write a float elevation/bathy grid as a single-band Float32 GeoTIFF.
bool writeElevationGeoTiff(const std::string& path, const core::RasterGrid& grid,
                           std::string* err = nullptr);

// Write an RGBA8 image as an RGB(A) GeoTIFF. rgba is width*height*4, row-major,
// top row first. geo_transform follows the GDAL convention; crs_wkt may be empty.
bool writeImageGeoTiff(const std::string& path,
                       uint32_t width, uint32_t height,
                       const std::vector<uint8_t>& rgba,
                       const double geo_transform[6],
                       const std::string& crs_wkt,
                       bool write_alpha = true,
                       std::string* err = nullptr);

} // namespace dolphin::io
