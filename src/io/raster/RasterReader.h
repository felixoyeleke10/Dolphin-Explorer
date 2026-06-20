#pragma once
// RasterReader — GeoTIFF / georeferenced-image ingestion via GDAL.
//
// Two product cases (see import flow):
//   * Elevation / depth raster  → single-band float grid  → core::RasterGrid
//   * Visual raster (RGB image) → RGBA pixels + geo-transform → RasterImage
//
// Kept Qt-free so it lives in the io layer (no App/UI dependency). The UI builds
// a QImage from RasterImage::rgba. GDAL handles GeoTIFF (incl. tiled/BigTIFF/
// compressed), world files (.pgw/.jgw/.wld) for plain PNG/JPG, and CRS (WKT).
#include "core/RasterGrid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dolphin::io {

enum class RasterKind {
    Unknown,
    Elevation,   // single-band continuous data → depth/bathy grid
    Image,       // RGB(A) (or grayscale) visual raster → map overlay
};

// Lightweight metadata from opening a raster — no pixel read.
struct RasterInfo {
    RasterKind  kind        = RasterKind::Unknown;
    int         bands       = 0;
    uint32_t    cols        = 0;
    uint32_t    rows        = 0;
    double      geo_transform[6] = {0, 1, 0, 0, 0, 1};   // GDAL convention
    bool        has_geo     = false;   // a real (non-identity) geo-transform exists
    std::string crs_wkt;               // OGC WKT, empty if none
    std::string driver;                // GDAL driver short name (e.g. "GTiff")
    std::string data_type;             // band 0 type, e.g. "Float32" / "Byte"
};

// Decoded visual raster: RGBA8, row-major, top row first.
struct RasterImage {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> rgba;                       // width * height * 4
    double               geo_transform[6] = {0, 1, 0, 0, 0, 1};
    bool                 has_geo = false;
    std::string          crs_wkt;
};

// Open + classify without reading pixels. nullopt if GDAL cannot open the file.
std::optional<RasterInfo> probeRaster(const std::string& path);

// Read a single-band raster as a float elevation/bathy grid. max_dim > 0 caps the
// output to that many pixels per axis — GDAL resamples (averages) on read, so a
// multi-GB raster never loads at full resolution. The geo-transform is adjusted
// for the decimation. max_dim == 0 reads at native resolution.
bool readElevationRaster(const std::string& path, core::RasterGrid& out,
                         std::string* err = nullptr, int max_dim = 0);

// Read a raster as an RGBA visual image (grayscale/RGB/RGBA all supported).
bool readImageRaster(const std::string& path, RasterImage& out,
                     std::string* err = nullptr, int max_dim = 0);

// Warped variants — reproject to WGS84 geographic (lon/lat) for the 2D map
// overlay, which works in the display (geographic) CRS. When the source has no
// CRS, these fall back to a native read (coordinates assumed already geographic).
// The returned geo_transform is therefore in degrees (lon/lat). max_dim caps size.
bool readElevationRasterWgs84(const std::string& path, core::RasterGrid& out,
                              std::string* err = nullptr, int max_dim = 0);
bool readImageRasterWgs84(const std::string& path, RasterImage& out,
                          std::string* err = nullptr, int max_dim = 0);

} // namespace dolphin::io
