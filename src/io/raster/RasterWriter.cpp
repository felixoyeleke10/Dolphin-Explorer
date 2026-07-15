// RasterWriter.cpp — GeoTIFF export via GDAL.
#include "io/raster/RasterWriter.h"
#include "util/AtomicFile.h"

#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_string.h>

#include <filesystem>
#include <mutex>

namespace dolphin::io {

namespace {

void ensureGdal()
{
    static std::once_flag once;
    std::call_once(once, [] { GDALAllRegister(); });
}

GDALDriver* gtiffDriver()
{
    return GetGDALDriverManager()->GetDriverByName("GTiff");
}

void setErr(std::string* err, const char* msg) { if (err) *err = msg; }

bool applyGeoref(GDALDataset* ds, const double gt[6], const std::string& wkt)
{
    // Only write a geo-transform if it isn't the identity placeholder.
    const bool identity = gt[0] == 0 && gt[1] == 1 && gt[2] == 0 &&
                          gt[3] == 0 && gt[4] == 0 && gt[5] == 1;
    if (!identity) {
        double g[6];
        for (int i = 0; i < 6; ++i) g[i] = gt[i];
        if (ds->SetGeoTransform(g) != CE_None) return false;
    }
    if (!wkt.empty() && ds->SetProjection(wkt.c_str()) != CE_None) return false;
    return true;
}

void discardCandidate(const std::filesystem::path& candidate)
{
    std::error_code ignored;
    std::filesystem::remove(candidate, ignored);
}

bool publishCandidate(const std::filesystem::path& candidate,
                      const std::filesystem::path& destination,
                      std::string* err)
{
    std::error_code ec;
    if (util::replaceFileAtomically(candidate, destination, ec)) return true;
    discardCandidate(candidate);
    const std::string message = ec
        ? "Could not publish the output GeoTIFF: " + ec.message()
        : "Could not publish the output GeoTIFF.";
    setErr(err, message.c_str());
    return false;
}

} // namespace

bool writeElevationGeoTiff(const std::string& path, const core::RasterGrid& grid,
                           std::string* err)
{
    ensureGdal();
    GDALDriver* drv = gtiffDriver();
    if (!drv) { setErr(err, "GDAL GTiff driver unavailable."); return false; }
    if (path.empty() || grid.cols == 0 || grid.rows == 0 ||
        grid.data.size() != static_cast<size_t>(grid.cols) * grid.rows) {
        setErr(err, "Elevation grid is empty or malformed.");
        return false;
    }

    const std::filesystem::path destination(path);
    const std::filesystem::path candidate = util::siblingTempPath(destination, "tif");
    const std::string candidate_name = candidate.string();

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    opts = CSLSetNameValue(opts, "PREDICTOR", "3");      // float predictor
    opts = CSLSetNameValue(opts, "TILED", "YES");
    opts = CSLSetNameValue(opts, "BIGTIFF", "IF_SAFER");

    GDALDataset* ds = drv->Create(candidate_name.c_str(),
                                  static_cast<int>(grid.cols),
                                  static_cast<int>(grid.rows),
                                  1, GDT_Float32, opts);
    CSLDestroy(opts);
    if (!ds) {
        discardCandidate(candidate);
        setErr(err, "Could not create the output GeoTIFF.");
        return false;
    }

    if (!applyGeoref(ds, grid.geo_transform, grid.crs_wkt)) {
        GDALClose(ds);
        discardCandidate(candidate);
        setErr(err, "GDAL failed to write raster georeferencing.");
        return false;
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band || band->SetNoDataValue(grid.no_data_value) != CE_None) {
        GDALClose(ds);
        discardCandidate(candidate);
        setErr(err, "GDAL failed to configure the elevation band.");
        return false;
    }

    const CPLErr e = band->RasterIO(
        GF_Write, 0, 0, static_cast<int>(grid.cols), static_cast<int>(grid.rows),
        const_cast<float*>(grid.data.data()),
        static_cast<int>(grid.cols), static_cast<int>(grid.rows),
        GDT_Float32, 0, 0);

    CPLErrorReset();
    GDALClose(ds);
    if (e != CE_None || CPLGetLastErrorType() >= CE_Failure) {
        discardCandidate(candidate);
        setErr(err, "GDAL failed to write raster pixels.");
        return false;
    }
    return publishCandidate(candidate, destination, err);
}

bool writeImageGeoTiff(const std::string& path,
                       uint32_t width, uint32_t height,
                       const std::vector<uint8_t>& rgba,
                       const double geo_transform[6],
                       const std::string& crs_wkt,
                       bool write_alpha,
                       std::string* err)
{
    ensureGdal();
    GDALDriver* drv = gtiffDriver();
    if (!drv) { setErr(err, "GDAL GTiff driver unavailable."); return false; }
    if (path.empty() || width == 0 || height == 0 ||
        rgba.size() != static_cast<size_t>(width) * height * 4) {
        setErr(err, "Image is empty or malformed.");
        return false;
    }

    const std::filesystem::path destination(path);
    const std::filesystem::path candidate = util::siblingTempPath(destination, "tif");
    const std::string candidate_name = candidate.string();

    const int nbands = write_alpha ? 4 : 3;

    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    opts = CSLSetNameValue(opts, "TILED", "YES");
    opts = CSLSetNameValue(opts, "PHOTOMETRIC", "RGB");
    opts = CSLSetNameValue(opts, "BIGTIFF", "IF_SAFER");

    GDALDataset* ds = drv->Create(candidate_name.c_str(),
                                  static_cast<int>(width),
                                  static_cast<int>(height),
                                  nbands, GDT_Byte, opts);
    CSLDestroy(opts);
    if (!ds) {
        discardCandidate(candidate);
        setErr(err, "Could not create the output GeoTIFF.");
        return false;
    }

    if (!applyGeoref(ds, geo_transform, crs_wkt)) {
        GDALClose(ds);
        discardCandidate(candidate);
        setErr(err, "GDAL failed to write image georeferencing.");
        return false;
    }

    const size_t px = static_cast<size_t>(width) * height;
    std::vector<uint8_t> plane(px);
    bool ok = true;
    for (int b = 0; b < nbands && ok; ++b) {
        for (size_t i = 0; i < px; ++i) plane[i] = rgba[i * 4 + b];
        GDALRasterBand* band = ds->GetRasterBand(b + 1);
        const GDALColorInterp ci =
            (b == 0) ? GCI_RedBand : (b == 1) ? GCI_GreenBand :
            (b == 2) ? GCI_BlueBand : GCI_AlphaBand;
        if (!band || band->SetColorInterpretation(ci) != CE_None
                || band->RasterIO(GF_Write, 0, 0, static_cast<int>(width), static_cast<int>(height),
                           plane.data(), static_cast<int>(width), static_cast<int>(height),
                           GDT_Byte, 0, 0) != CE_None)
            ok = false;
    }

    CPLErrorReset();
    GDALClose(ds);
    if (!ok || CPLGetLastErrorType() >= CE_Failure) {
        discardCandidate(candidate);
        setErr(err, "GDAL failed to write image pixels.");
        return false;
    }
    return publishCandidate(candidate, destination, err);
}

} // namespace dolphin::io
