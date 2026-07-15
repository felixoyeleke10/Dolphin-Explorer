// Round-trip tests for the GDAL-backed raster reader/writer.
//   - elevation GeoTIFF: write float grid -> read back -> pixels/geo-transform/nodata match
//   - probe classification: elevation vs image
//   - image GeoTIFF: write RGBA -> read back -> pixels/geo-transform match
//
// No CRS is set, so the test needs only the GTiff driver + GDAL DLLs (no PROJ data).
// Minimal CHECK harness. Entry: ctest --output-on-failure
#include "io/raster/RasterReader.h"
#include "io/raster/RasterWriter.h"
#include "core/RasterGrid.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond) ++g_pass;
    else { ++g_fail; std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expr); }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)

using namespace dolphin;

static std::string tmpPath(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

static void testElevationRoundTrip()
{
    core::RasterGrid g;
    g.cols = 8; g.rows = 6;
    g.no_data_value = -9999.f;
    // North-up geo-transform: 2 m pixels, top-left at (500000, 4000000).
    g.geo_transform[0] = 500000.0; g.geo_transform[1] = 2.0; g.geo_transform[2] = 0.0;
    g.geo_transform[3] = 4000000.0; g.geo_transform[4] = 0.0; g.geo_transform[5] = -2.0;
    g.data.resize(static_cast<size_t>(g.cols) * g.rows);
    for (uint32_t r = 0; r < g.rows; ++r)
        for (uint32_t c = 0; c < g.cols; ++c)
            g.data[r * g.cols + c] = static_cast<float>(r * 10 + c) + 0.5f;
    g.data[0] = g.no_data_value;   // one hole

    const std::string path = tmpPath("dolphin_elev.tif");
    std::string err;
    CHECK(io::writeElevationGeoTiff(path, g, &err));

    auto info = io::probeRaster(path);
    CHECK(info.has_value());
    if (info) {
        CHECK(info->kind == io::RasterKind::Elevation);
        CHECK(info->bands == 1);
        CHECK(info->cols == 8 && info->rows == 6);
        CHECK(info->has_geo);
    }

    core::RasterGrid back;
    CHECK(io::readElevationRaster(path, back, &err));
    CHECK(back.cols == g.cols && back.rows == g.rows);
    CHECK(back.data.size() == g.data.size());
    bool pixels_ok = back.data.size() == g.data.size();
    for (size_t i = 0; i < back.data.size() && pixels_ok; ++i)
        if (std::fabs(back.data[i] - g.data[i]) > 1e-3f) pixels_ok = false;
    CHECK(pixels_ok);
    CHECK(std::fabs(back.geo_transform[0] - 500000.0) < 1e-6);
    CHECK(std::fabs(back.geo_transform[1] - 2.0) < 1e-9);
    CHECK(std::fabs(back.geo_transform[5] + 2.0) < 1e-9);
    CHECK(std::fabs(back.no_data_value - g.no_data_value) < 1e-3f);

    // A second valid export must atomically replace the existing GeoTIFF on
    // Windows as well as POSIX.
    core::RasterGrid replacement = g;
    replacement.cols = 3;
    replacement.rows = 2;
    replacement.data = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    CHECK(io::writeElevationGeoTiff(path, replacement, &err));
    core::RasterGrid replaced;
    CHECK(io::readElevationRaster(path, replaced, &err));
    CHECK(replaced.cols == 3 && replaced.rows == 2);
    CHECK(replaced.data.size() == 6);
    CHECK(replaced.data.size() == 6 && std::fabs(replaced.data.back() - 6.f) < 1e-3f);

    // A rejected candidate must leave that durable destination intact.
    core::RasterGrid malformed = replacement;
    malformed.data.pop_back();
    CHECK(!io::writeElevationGeoTiff(path, malformed, &err));
    core::RasterGrid preserved;
    CHECK(io::readElevationRaster(path, preserved, &err));
    CHECK(preserved.cols == 3 && preserved.rows == 2);
    CHECK(preserved.data.size() == 6
          && std::fabs(preserved.data.back() - 6.f) < 1e-3f);

    std::filesystem::remove(path);
}

static void testImageRoundTrip()
{
    const uint32_t w = 8, h = 6;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
    for (uint32_t r = 0; r < h; ++r)
        for (uint32_t c = 0; c < w; ++c) {
            const size_t k = (static_cast<size_t>(r) * w + c) * 4;
            rgba[k+0] = static_cast<uint8_t>(c * 30);
            rgba[k+1] = static_cast<uint8_t>(r * 40);
            rgba[k+2] = 200;
            rgba[k+3] = 255;
        }
    double gt[6] = {10.0, 0.5, 0.0, 50.0, 0.0, -0.5};

    const std::string path = tmpPath("dolphin_img.tif");
    std::string err;
    CHECK(io::writeImageGeoTiff(path, w, h, rgba, gt, /*crs*/ std::string(),
                               /*write_alpha*/ true, &err));

    auto info = io::probeRaster(path);
    CHECK(info.has_value());
    if (info) {
        CHECK(info->kind == io::RasterKind::Image);
        CHECK(info->bands >= 3);
        CHECK(info->cols == w && info->rows == h);
    }

    io::RasterImage back;
    CHECK(io::readImageRaster(path, back, &err));
    CHECK(back.width == w && back.height == h);
    CHECK(back.rgba.size() == static_cast<size_t>(w) * h * 4);
    bool px_ok = back.rgba.size() == rgba.size();
    // Check RGB of a couple of pixels (alpha may be dropped/!present depending on driver).
    auto rgbEq = [&](size_t k) {
        return back.rgba[k+0] == rgba[k+0] && back.rgba[k+1] == rgba[k+1] &&
               back.rgba[k+2] == rgba[k+2];
    };
    if (px_ok) { px_ok = rgbEq(0) && rgbEq((static_cast<size_t>(h-1)*w + (w-1))*4); }
    CHECK(px_ok);
    CHECK(back.has_geo);
    CHECK(std::fabs(back.geo_transform[1] - 0.5) < 1e-9);

    std::filesystem::remove(path);
}

// Big-file rigor: a max_dim cap must downsample on read (GDAL averaging) and scale
// the geo-transform so the geographic extent is preserved.
static void testDecimatedRead()
{
    core::RasterGrid g;
    g.cols = 100; g.rows = 60; g.no_data_value = -9999.f;
    g.geo_transform[0] = 0; g.geo_transform[1] = 2; g.geo_transform[2] = 0;
    g.geo_transform[3] = 0; g.geo_transform[4] = 0; g.geo_transform[5] = -2;
    g.data.assign(static_cast<size_t>(100) * 60, 5.0f);   // flat 5 m → average stays 5

    const std::string path = tmpPath("dolphin_decim.tif");
    std::string err;
    CHECK(io::writeElevationGeoTiff(path, g, &err));

    core::RasterGrid back;
    CHECK(io::readElevationRaster(path, back, &err, /*max_dim*/ 50));
    CHECK(back.cols == 50 && back.rows == 30);             // proportional cap
    CHECK(std::fabs(back.geo_transform[1] - 4.0) < 1e-6);  // pixel size doubled
    CHECK(std::fabs(back.geo_transform[5] + 4.0) < 1e-6);
    // Geographic extent preserved despite decimation.
    CHECK(std::fabs(back.cols * back.geo_transform[1] - 100 * 2.0) < 1e-6);
    bool flat = !back.data.empty();
    for (float v : back.data) if (std::fabs(v - 5.f) > 1e-2f) flat = false;
    CHECK(flat);

    std::filesystem::remove(path);
}

// CRS correctness: a projected (UTM) raster, read warped, must come back in WGS84
// degrees (lon near the zone's central meridian) — proving the GDAL/PROJ reprojection
// path (and the bundled PROJ data) works, not just a passthrough.
static void testWarpReproject()
{
    // WGS 84 / UTM zone 31N (central meridian 3°E).
    static const char* kUtm31N =
        "PROJCS[\"WGS 84 / UTM zone 31N\",GEOGCS[\"WGS 84\","
        "DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]],"
        "PROJECTION[\"Transverse_Mercator\"],PARAMETER[\"latitude_of_origin\",0],"
        "PARAMETER[\"central_meridian\",3],PARAMETER[\"scale_factor\",0.9996],"
        "PARAMETER[\"false_easting\",500000],PARAMETER[\"false_northing\",0],"
        "UNIT[\"metre\",1],AXIS[\"Easting\",EAST],AXIS[\"Northing\",NORTH]]";

    core::RasterGrid g;
    g.cols = 20; g.rows = 20; g.no_data_value = -9999.f;
    g.geo_transform[0] = 500000.0; g.geo_transform[1] = 10.0; g.geo_transform[2] = 0.0;
    g.geo_transform[3] = 100000.0; g.geo_transform[4] = 0.0; g.geo_transform[5] = -10.0;
    g.crs_wkt = kUtm31N;
    g.data.assign(static_cast<size_t>(20) * 20, 3.0f);

    const std::string path = tmpPath("dolphin_utm.tif");
    std::string err;
    CHECK(io::writeElevationGeoTiff(path, g, &err));

    core::RasterGrid back;
    CHECK(io::readElevationRasterWgs84(path, back, &err));
    // Reprojected to degrees: longitude near the 3°E central meridian, pixel size now
    // a fraction of a degree (not 10 metres). If PROJ data were missing the warp would
    // fall back to native and these would fail (still 500000 / 10).
    CHECK(back.geo_transform[0] > 2.0 && back.geo_transform[0] < 4.0);
    CHECK(std::fabs(back.geo_transform[1]) < 0.01);
    CHECK(back.cols > 0 && back.rows > 0);

    core::RasterGrid web_mercator;
    CHECK(io::readElevationRasterForCrs(
        path, "EPSG:3857", web_mercator, &err));
    // 3°E in Web Mercator is roughly 334 km east of the prime meridian.
    CHECK(web_mercator.geo_transform[0] > 300000.0
          && web_mercator.geo_transform[0] < 370000.0);
    CHECK(std::fabs(web_mercator.geo_transform[1]) > 1.0);

    core::RasterGrid rejected;
    CHECK(!io::readElevationRasterForCrs(
        path, "NOT_A_REAL_CRS", rejected, &err));

    std::filesystem::remove(path);
}

// Big-file safeguard: a large raster read with a cap returns a small grid quickly
// (GDAL resamples on read — the full grid is never materialised).
static void testLargeDecimated()
{
    core::RasterGrid g;
    g.cols = 2000; g.rows = 2000; g.no_data_value = -9999.f;
    g.geo_transform[0] = 0; g.geo_transform[1] = 1; g.geo_transform[2] = 0;
    g.geo_transform[3] = 0; g.geo_transform[4] = 0; g.geo_transform[5] = -1;
    g.data.assign(static_cast<size_t>(2000) * 2000, 7.0f);

    const std::string path = tmpPath("dolphin_big.tif");
    std::string err;
    CHECK(io::writeElevationGeoTiff(path, g, &err));

    core::RasterGrid back;
    CHECK(io::readElevationRaster(path, back, &err, /*max_dim*/ 256));
    CHECK(back.cols <= 256 && back.rows <= 256);
    CHECK(static_cast<size_t>(back.cols) * back.rows <= 256u * 256u);   // not the full 4M
    CHECK(std::fabs(back.cols * back.geo_transform[1] - 2000 * 1.0) < 1.0);  // extent kept

    std::filesystem::remove(path);
}

int main()
{
    testElevationRoundTrip();
    testImageRoundTrip();
    testDecimatedRead();
    testWarpReproject();
    testLargeDecimated();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
