#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dolphin::core {

enum class RasterBand : uint8_t {
    Bathymetry,   // metres depth, positive down
    Backscatter,  // dB
    Uncertainty,
    Custom,
};

// A geo-referenced single-band raster (imported GeoTIFF, gridded bathy, etc.)
struct RasterGrid {
    uint64_t    id            = 0;
    int64_t     timestamp_us  = 0;   // µs since epoch — time of creation/import
    std::string source_id;
    RasterBand  band          = RasterBand::Bathymetry;

    // GDAL geo-transform convention:
    //   [0] = top-left X (easting/longitude)
    //   [1] = pixel width (X resolution)
    //   [2] = row rotation (0 for north-up)
    //   [3] = top-left Y (northing/latitude)
    //   [4] = column rotation (0 for north-up)
    //   [5] = pixel height (negative for north-up)
    double      geo_transform[6] = {};
    std::string crs_wkt;             // coordinate reference system (OGC WKT)
    uint32_t    cols = 0, rows = 0;
    float       no_data_value = -9999.f;
    std::vector<float> data;         // row-major, cols × rows elements
};

} // namespace dolphin::core
