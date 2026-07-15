// MainWindow.LayerCoordinator.Raster.cpp — raster decoding and map display.
#include "ui/mainwindow/MainWindow.h"

#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "io/raster/RasterReader.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/track/TrackMapBuild.h"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace dolphin::ui {
namespace {

// Set the overlay bounds from a display-CRS geo-transform. Four-corner bounds
// also handle rotated rasters rather than assuming north-up pixels.
void setRasterGeoBounds(LayerMapData& ld, const double gt[6],
                        uint32_t cols, uint32_t rows, bool is_projected)
{
    auto xAt = [&](double c, double r) { return gt[0] + c * gt[1] + r * gt[2]; };
    auto yAt = [&](double c, double r) { return gt[3] + c * gt[4] + r * gt[5]; };
    const double xs[] = {xAt(0, 0), xAt(cols, 0), xAt(0, rows), xAt(cols, rows)};
    const double ys[] = {yAt(0, 0), yAt(cols, 0), yAt(0, rows), yAt(cols, rows)};
    const auto [xmin, xmax] = std::minmax_element(std::begin(xs), std::end(xs));
    const auto [ymin, ymax] = std::minmax_element(std::begin(ys), std::end(ys));
    ld.lon_min = *xmin;  ld.lon_max = *xmax;
    ld.lat_min = *ymin;  ld.lat_max = *ymax;
    ld.is_projected = is_projected;
    ld.visible      = true;
    ld.kind         = LayerMapKind::Swath;   // any kind with a preview_image renders the image
}

// Colourise a depth/bathy grid into an RGBA image (no-data → transparent).
// Bathymetric ramp: deep = dark blue → shallow = warm. Depth is positive-down.
QImage colorizeDepthGrid(const core::RasterGrid& g)
{
    QImage img(static_cast<int>(g.cols), static_cast<int>(g.rows), QImage::Format_RGBA8888);
    img.fill(Qt::transparent);

    float zmin = 1e30f, zmax = -1e30f;
    for (float v : g.data)
        if (std::isfinite(v) && v != g.no_data_value) { zmin = std::min(zmin, v); zmax = std::max(zmax, v); }
    if (!(zmax > zmin)) zmax = zmin + 1.f;
    const float inv = 1.f / (zmax - zmin);

    struct Stop { float p; int r, g, b; };
    static const Stop ramp[] = {
        {0.00f,   8,  24,  80}, {0.25f,  16,  78, 160}, {0.50f,  30, 160, 176},
        {0.75f, 120, 200,  90}, {1.00f, 240, 230, 140},
    };
    auto shade = [&](float t) -> QRgb {
        t = std::clamp(t, 0.f, 1.f);
        for (int i = 1; i < 5; ++i)
            if (t <= ramp[i].p) {
                const float f = (t - ramp[i-1].p) / (ramp[i].p - ramp[i-1].p);
                return qRgb(int(ramp[i-1].r + f * (ramp[i].r - ramp[i-1].r)),
                            int(ramp[i-1].g + f * (ramp[i].g - ramp[i-1].g)),
                            int(ramp[i-1].b + f * (ramp[i].b - ramp[i-1].b)));
            }
        return qRgb(ramp[4].r, ramp[4].g, ramp[4].b);
    };

    for (uint32_t r = 0; r < g.rows; ++r) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(static_cast<int>(r)));
        for (uint32_t c = 0; c < g.cols; ++c) {
            const float v = g.data[static_cast<size_t>(r) * g.cols + c];
            if (!std::isfinite(v) || v == g.no_data_value) { line[c] = qRgba(0,0,0,0); continue; }
            const float t = 1.f - (v - zmin) * inv;   // shallow (small depth) → warm
            const QRgb s = shade(t);
            line[c] = qRgba(qRed(s), qGreen(s), qBlue(s), 255);
        }
    }
    return img;
}

struct RasterDisplayResult {
    LayerMapData    map_data;
    core::RasterGrid terrain;
    bool             has_terrain = false;
    std::string      error;
};

} // namespace

// Display a raster layer. Decoding and reprojection run off the UI thread. Both
// 2D and 3D representations use the project display CRS so mixed raster/sonar
// projects share one coordinate space.
void MainWindow::displayRaster(app::DataLayer* layer)
{
    if (!layer || !layer->raster.valid || !currentProject() || !m_op_mgr) return;
    const std::string path = layer->artifact_store_path;
    const std::string lid   = layer->id;
    const QString label     = QString::fromStdString(layer->label);
    const bool is_depth     = layer->raster.is_depth;
    const bool layer_visible = layer->visible;
    const core::SpatialRef display_ref = currentProject()->displaySpatialRef();
    const bool is_projected = display_ref.exact
                           && core::spatialRefIsProjected(display_ref);
    const std::string target_crs = display_ref.exact && !display_ref.id.empty()
        ? display_ref.id : std::string("EPSG:4326");

    m_op_mgr->run<RasterDisplayResult>(
        tr("Loading raster %1…").arg(label),
        [path, is_depth, is_projected, target_crs,
         layer_visible](app::CancellationToken token) {
            RasterDisplayResult result;
            result.map_data.visible = layer_visible;
            if (token.isCancelled()) return result;

            if (is_depth) {
                core::RasterGrid grid;
                if (!io::readElevationRasterForCrs(
                        path, target_crs, grid, &result.error, /*max_dim*/ 2048)
                        || grid.cols == 0 || grid.rows == 0) {
                    if (result.error.empty()) result.error = "Raster contains no pixels.";
                    return result;
                }
                result.map_data.preview_image = colorizeDepthGrid(grid);
                setRasterGeoBounds(result.map_data, grid.geo_transform,
                                   grid.cols, grid.rows, is_projected);
                result.terrain = std::move(grid);
                result.has_terrain = true;
            } else {
                io::RasterImage image;
                if (!io::readImageRasterForCrs(
                        path, target_crs, image, &result.error, /*max_dim*/ 4096)
                        || image.width == 0 || image.height == 0) {
                    if (result.error.empty()) result.error = "Raster contains no pixels.";
                    return result;
                }
                const QImage pixels(
                    image.rgba.data(), static_cast<int>(image.width),
                    static_cast<int>(image.height), QImage::Format_RGBA8888);
                result.map_data.preview_image = pixels.copy();
                setRasterGeoBounds(result.map_data, image.geo_transform,
                                   image.width, image.height, is_projected);
            }
            return result;
        },
        [this, lid, label](RasterDisplayResult result) {
            if (!currentProject() || !currentProject()->findLayer(lid)) return;
            if (!result.error.empty()) {
                appendJobMessage(tr("Raster load failed — %1")
                    .arg(QString::fromStdString(result.error)));
                return;
            }
            if (result.has_terrain && m_viewport_host)
                m_viewport_host->loadRasterTerrain(lid, result.terrain);
            if (m_map_view)
                m_map_view->setLayerMapData(lid, std::move(result.map_data));
            appendJobMessage(tr("Raster on map — %1").arg(label));
        },
        "raster_display:" + lid,
        /*heavy=*/false);
}

} // namespace dolphin::ui
