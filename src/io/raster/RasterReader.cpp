// RasterReader.cpp — GDAL-backed raster ingestion (GeoTIFF, image+world-file).
#include "io/raster/RasterReader.h"

#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>

#include <mutex>

namespace dolphin::io {

namespace {

void ensureGdal()
{
    static std::once_flag once;
    std::call_once(once, [] {
        CPLSetConfigOption("GDAL_NUM_THREADS", "ALL_CPUS");
        GDALAllRegister();
    });
}

// Single-band rasters: float continuous data → elevation; 8-bit → visual image.
RasterKind classifyKind(int bands, GDALDataType dt)
{
    if (bands >= 2) return RasterKind::Image;     // RGB / RGBA / gray+alpha
    if (dt == GDT_Byte) return RasterKind::Image; // single 8-bit band → visual
    return RasterKind::Elevation;                 // single continuous band → grid
}

struct GdalDs {
    GDALDataset* ds = nullptr;
    explicit GdalDs(const std::string& path) {
        ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    }
    ~GdalDs() { if (ds) GDALClose(ds); }
    explicit operator bool() const { return ds != nullptr; }
    GDALDataset* operator->() const { return ds; }
};

void setErr(std::string* err, const char* msg) { if (err) *err = msg; }

// Reproject a dataset to WGS84 geographic (lon/lat). Returns a warped VRT the
// caller must GDALClose, or nullptr when the source has no CRS (warp impossible).
GDALDataset* warpToWgs84(GDALDataset* src)
{
    const char* srcWkt = src->GetProjectionRef();
    if (!srcWkt || !*srcWkt) return nullptr;

    OGRSpatialReference dst;
    dst.SetWellKnownGeogCS("WGS84");
    dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);   // lon, lat order
    char* dstWkt = nullptr;
    dst.exportToWkt(&dstWkt);

    GDALDataset* warped = static_cast<GDALDataset*>(GDALAutoCreateWarpedVRT(
        src, srcWkt, dstWkt, GRA_NearestNeighbour, 0.0, nullptr));
    CPLFree(dstWkt);
    return warped;
}

// -- Read implementations operating on an already-open dataset ----------------
// (lets the native and warped-to-WGS84 paths share one code path).

// Compute the decimated output size + per-axis scale for a max_dim cap.
void decimatedSize(uint32_t cols, uint32_t rows, int max_dim,
                   uint32_t& outW, uint32_t& outH, double& sx, double& sy)
{
    outW = cols; outH = rows; sx = 1.0; sy = 1.0;
    if (max_dim > 0 && (cols > static_cast<uint32_t>(max_dim) ||
                        rows > static_cast<uint32_t>(max_dim))) {
        const double s = static_cast<double>(max_dim) / std::max(cols, rows);
        outW = std::max<uint32_t>(1, static_cast<uint32_t>(cols * s + 0.5));
        outH = std::max<uint32_t>(1, static_cast<uint32_t>(rows * s + 0.5));
        sx = static_cast<double>(cols) / outW;
        sy = static_cast<double>(rows) / outH;
    }
}

// Scale a geo-transform's pixel size for a decimated read (top-left unchanged).
void scaleGeoTransform(double gt[6], double sx, double sy)
{
    gt[1] *= sx; gt[2] *= sy; gt[4] *= sx; gt[5] *= sy;
}

bool fillElevation(GDALDataset* ds, core::RasterGrid& out, std::string* err, int max_dim)
{
    if (ds->GetRasterCount() < 1) { setErr(err, "Raster has no bands."); return false; }
    const uint32_t cols = static_cast<uint32_t>(ds->GetRasterXSize());
    const uint32_t rows = static_cast<uint32_t>(ds->GetRasterYSize());
    if (cols == 0 || rows == 0) { setErr(err, "Raster has zero extent."); return false; }

    uint32_t outW, outH; double sx, sy;
    decimatedSize(cols, rows, max_dim, outW, outH, sx, sy);

    GDALRasterBand* band = ds->GetRasterBand(1);
    out.cols = outW;
    out.rows = outH;
    out.band = core::RasterBand::Bathymetry;
    out.data.assign(static_cast<size_t>(outW) * outH, 0.f);

    GDALRasterIOExtraArg extra;
    INIT_RASTERIO_EXTRA_ARG(extra);
    extra.eResampleAlg = GRIORA_Average;   // honours band nodata when averaging
    if (band->RasterIO(GF_Read, 0, 0, cols, rows, out.data.data(),
                       static_cast<int>(outW), static_cast<int>(outH),
                       GDT_Float32, 0, 0, &extra) != CE_None) {
        setErr(err, "GDAL failed to read raster pixels."); return false;
    }

    int has_nodata = 0;
    const double nd = band->GetNoDataValue(&has_nodata);
    if (has_nodata) out.no_data_value = static_cast<float>(nd);

    double gt[6] = {0, 1, 0, 0, 0, 1};
    if (ds->GetGeoTransform(gt) == CE_None) {
        scaleGeoTransform(gt, sx, sy);
        for (int i = 0; i < 6; ++i) out.geo_transform[i] = gt[i];
    }
    if (const char* wkt = ds->GetProjectionRef(); wkt && *wkt)
        out.crs_wkt = wkt;
    return true;
}

bool fillImage(GDALDataset* ds, RasterImage& out, std::string* err, int max_dim)
{
    const int      nbands = ds->GetRasterCount();
    const uint32_t cols   = static_cast<uint32_t>(ds->GetRasterXSize());
    const uint32_t rows   = static_cast<uint32_t>(ds->GetRasterYSize());
    if (nbands < 1 || cols == 0 || rows == 0) {
        setErr(err, "Raster has no image data."); return false;
    }

    uint32_t outW, outH; double sx, sy;
    decimatedSize(cols, rows, max_dim, outW, outH, sx, sy);

    out.width  = outW;
    out.height = outH;
    out.rgba.assign(static_cast<size_t>(outW) * outH * 4, 255);
    const size_t px = static_cast<size_t>(outW) * outH;
    std::vector<uint8_t> plane(px);

    GDALRasterIOExtraArg extra;
    INIT_RASTERIO_EXTRA_ARG(extra);
    extra.eResampleAlg = GRIORA_Average;

    auto readBandInto = [&](int gdal_band, int rgba_offset) -> bool {
        GDALRasterBand* b = ds->GetRasterBand(gdal_band);
        if (b->RasterIO(GF_Read, 0, 0, cols, rows, plane.data(),
                        static_cast<int>(outW), static_cast<int>(outH),
                        GDT_Byte, 0, 0, &extra) != CE_None)
            return false;
        for (size_t i = 0; i < px; ++i) out.rgba[i * 4 + rgba_offset] = plane[i];
        return true;
    };

    bool ok = true;
    if (nbands == 1) {
        ok = readBandInto(1, 0);
        if (ok) for (size_t i = 0; i < px; ++i) {
            out.rgba[i * 4 + 1] = out.rgba[i * 4 + 0];
            out.rgba[i * 4 + 2] = out.rgba[i * 4 + 0];
        }
    } else if (nbands == 2) {
        ok = readBandInto(1, 0);
        if (ok) for (size_t i = 0; i < px; ++i) {
            out.rgba[i * 4 + 1] = out.rgba[i * 4 + 0];
            out.rgba[i * 4 + 2] = out.rgba[i * 4 + 0];
        }
        if (ok) ok = readBandInto(2, 3);
    } else {
        ok = readBandInto(1, 0) && readBandInto(2, 1) && readBandInto(3, 2);
        if (ok && nbands >= 4) ok = readBandInto(4, 3);
    }
    if (!ok) { setErr(err, "GDAL failed to read image pixels."); return false; }

    double gt[6] = {0, 1, 0, 0, 0, 1};
    if (ds->GetGeoTransform(gt) == CE_None) {
        scaleGeoTransform(gt, sx, sy);
        out.has_geo = true;
        for (int i = 0; i < 6; ++i) out.geo_transform[i] = gt[i];
    }
    if (const char* wkt = ds->GetProjectionRef(); wkt && *wkt)
        out.crs_wkt = wkt;
    return true;
}

} // namespace

std::optional<RasterInfo> probeRaster(const std::string& path)
{
    ensureGdal();
    GdalDs ds(path);
    if (!ds) return std::nullopt;

    RasterInfo info;
    info.bands = ds->GetRasterCount();
    info.cols  = static_cast<uint32_t>(ds->GetRasterXSize());
    info.rows  = static_cast<uint32_t>(ds->GetRasterYSize());

    double gt[6] = {0, 1, 0, 0, 0, 1};
    if (ds->GetGeoTransform(gt) == CE_None) {
        info.has_geo = true;
        for (int i = 0; i < 6; ++i) info.geo_transform[i] = gt[i];
    }
    if (const char* wkt = ds->GetProjectionRef(); wkt && *wkt)
        info.crs_wkt = wkt;
    if (auto* drv = ds->GetDriver())
        info.driver = drv->GetDescription();

    GDALDataType dt = GDT_Byte;
    if (info.bands > 0) {
        dt = ds->GetRasterBand(1)->GetRasterDataType();
        info.data_type = GDALGetDataTypeName(dt);
    }
    info.kind = classifyKind(info.bands, dt);
    return info;
}

bool readElevationRaster(const std::string& path, core::RasterGrid& out, std::string* err, int max_dim)
{
    ensureGdal();
    GdalDs ds(path);
    if (!ds) { setErr(err, "GDAL could not open the raster."); return false; }
    return fillElevation(ds.ds, out, err, max_dim);
}

bool readImageRaster(const std::string& path, RasterImage& out, std::string* err, int max_dim)
{
    ensureGdal();
    GdalDs ds(path);
    if (!ds) { setErr(err, "GDAL could not open the raster."); return false; }
    return fillImage(ds.ds, out, err, max_dim);
}

bool readElevationRasterWgs84(const std::string& path, core::RasterGrid& out, std::string* err, int max_dim)
{
    ensureGdal();
    GdalDs ds(path);
    if (!ds) { setErr(err, "GDAL could not open the raster."); return false; }
    GDALDataset* warped = warpToWgs84(ds.ds);
    const bool ok = fillElevation(warped ? warped : ds.ds, out, err, max_dim);
    if (warped) GDALClose(warped);
    return ok;
}

bool readImageRasterWgs84(const std::string& path, RasterImage& out, std::string* err, int max_dim)
{
    ensureGdal();
    GdalDs ds(path);
    if (!ds) { setErr(err, "GDAL could not open the raster."); return false; }
    GDALDataset* warped = warpToWgs84(ds.ds);
    const bool ok = fillImage(warped ? warped : ds.ds, out, err, max_dim);
    if (warped) GDALClose(warped);
    return ok;
}

} // namespace dolphin::io
