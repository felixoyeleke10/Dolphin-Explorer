// MapView.cpp — data API, coordinate helpers, nav-track assembly.
//
// Painting:    MapViewPaint.cpp           (paintEvent)
// Input:       MapViewInput.cpp           (resizeEvent, mouse/wheel/leave events)
// Swath build (runs off UI thread): SssMapBuild.cpp

#include "ui/features/map/MapView.h"
#include "ui/features/map/track/TrackMapBuild.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Artifact.h"
#include "core/Contact.h"
#include "geo/GeoUtils.h"
#include "ui/shell/Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace dolphin::ui {

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr double kDegToRad  = M_PI / 180.0;
static constexpr double kPixPerDeg = 6.0;
static constexpr double kPixPerM   = 0.5;

// ── Constructor ───────────────────────────────────────────────────────────────

MapView::MapView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
}


void MapView::setShowGrid(bool show)
{
    if (m_show_grid == show) return;
    m_show_grid = show;
    update();
}

void MapView::setMapBgColor(QColor c)
{
    if (m_map_bg_color == c) return;
    m_map_bg_color = c;
    update();
}

void MapView::setGridColor(QColor c)
{
    if (m_grid_color == c) return;
    m_grid_color = c;
    update();
}

void MapView::setGratLabelSize(int size)
{
    if (m_grat_label_size == size) return;
    m_grat_label_size = size;
    update();
}

void MapView::setGratLabelRotated(bool rotated)
{
    if (m_grat_label_rotated == rotated) return;
    m_grat_label_rotated = rotated;
    update();
}

void MapView::setGratCoordFormat(int fmt)
{
    if (m_grat_coord_fmt == fmt) return;
    m_grat_coord_fmt = fmt;
    update();
}

// ── Data API ──────────────────────────────────────────────────────────────────

void MapView::setProject(app::Project* project)
{
    m_project = project;
    m_active_layer_id.clear();
    m_layer_data.clear();
    m_nav_track.clear();
    m_needs_fit       = false;
    m_user_interacted = false;
    m_bbox_lon_min =  1e18; m_bbox_lon_max = -1e18;
    m_bbox_lat_min =  1e18; m_bbox_lat_max = -1e18;
    update();
}

void MapView::setActiveLayer(const std::string& layer_id)
{
    m_active_layer_id = layer_id;
    rebuildNavTrack();
}

void MapView::refreshLayerOrder()
{
    rebuildCombined();
    update();
}

double MapView::baseScale() const
{
    return m_is_projected ? kPixPerM : kPixPerDeg;
}

void MapView::setSelectedContact(uint64_t id)
{
    if (m_selected_contact_id == id) return;
    m_selected_contact_id = id;
    update();
}

// ── rebuildNavTrack ───────────────────────────────────────────────────────────

void MapView::rebuildNavTrack()
{
    if (m_active_layer_id.empty() || !m_project) {
        rebuildCombined();
        update();
        return;
    }

    // Layer data already present: skip synchronous rebuild.
    //   SSS: has coverage ribbons from background build.
    //   Track layers (SBP/MAG/MBE): either async result arrived or a loading
    //   placeholder was inserted — either way the sync build is unnecessary.
    auto it = m_layer_data.find(m_active_layer_id);
    if (it != m_layer_data.end()) {
        const LayerMapKind k = it->second.kind;
        if (!it->second.coverage.empty()
                || k == LayerMapKind::Track
                || k == LayerMapKind::Profile) {
            rebuildCombined();
            update();
            return;
        }
    }

    const app::DataLayer* layer = m_project->findLayer(m_active_layer_id);
    if (!layer || !layer->index_built || layer->artifact_index.empty()) {
        rebuildCombined();
        update();
        return;
    }

    core::SpatialRef source_ref = layer->source_spatial_ref;
    if (source_ref.empty()) {
        if (const auto* src = m_project->findSource(layer->source_id))
            source_ref = src->source_spatial_ref;
    }
    const core::SpatialRef display_ref = m_project->displaySpatialRef();

    const core::ArtifactType type_filter =
        app::artifactTypeForModality(layer->modality);

    LayerMapData built = buildTrackLayerMapData(
        layer->artifact_index, type_filter, source_ref, display_ref);

    // Preserve view-state that lives on the main thread.
    LayerMapData& ld        = m_layer_data[m_active_layer_id];
    const bool visible      = ld.visible;
    const bool show_nav     = ld.show_nav_track;
    ld                      = std::move(built);
    ld.visible              = visible;
    ld.show_nav_track       = show_nav;

    rebuildCombined();
    if (ld.track_stats.track_points > 0)
        fitToData();
    update();
}

// ── rebuildCombined ───────────────────────────────────────────────────────────

void MapView::rebuildCombined()
{
    m_nav_track.clear();
    m_bbox_lon_min =  1e18; m_bbox_lon_max = -1e18;
    m_bbox_lat_min =  1e18; m_bbox_lat_max = -1e18;
    m_is_projected = false;
    bool first_layer_seen = false;

    auto appendLayerData = [&](const LayerMapData& data) {
        if (data.show_nav_track && !data.nav_track.empty()) {
            if (!m_nav_track.empty())
                m_nav_track.push_back({std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::quiet_NaN()});
            m_nav_track.insert(m_nav_track.end(),
                               data.nav_track.begin(), data.nav_track.end());
        }
        m_bbox_lon_min = std::min(m_bbox_lon_min, data.lon_min);
        m_bbox_lon_max = std::max(m_bbox_lon_max, data.lon_max);
        m_bbox_lat_min = std::min(m_bbox_lat_min, data.lat_min);
        m_bbox_lat_max = std::max(m_bbox_lat_max, data.lat_max);
        // First visible layer's CRS wins: all layers should share a display CRS,
        // but if they somehow differ, the first layer's flag avoids polluting
        // the scale bar and cursor for layers that are genuinely geographic.
        if (!first_layer_seen) {
            m_is_projected    = data.is_projected;
            first_layer_seen  = true;
        }
    };

    if (m_project) {
        for (const auto& layer : m_project->layers()) {
            if (!layer) continue;
            const auto it = m_layer_data.find(layer->id);
            if (it == m_layer_data.end() || !it->second.visible) continue;
            appendLayerData(it->second);
        }
    } else {
        for (const auto& [id, data] : m_layer_data)
            if (data.visible) appendLayerData(data);
    }
}

// ── fitToData / fitToDataAndReset ─────────────────────────────────────────────

void MapView::fitToDataAndReset()
{
    m_user_interacted = false;
    fitToData();
}

void MapView::fitToData()
{
    if (m_bbox_lon_min > m_bbox_lon_max || m_bbox_lat_min > m_bbox_lat_max) return;

    if (width() <= 0 || height() <= 0) { m_needs_fit = true; return; }

    m_ref_lat = (m_bbox_lat_min + m_bbox_lat_max) * 0.5;
    const double cos_ref = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_ref_lat * kDegToRad));

    double dlon = m_bbox_lon_max - m_bbox_lon_min;
    double dlat = m_bbox_lat_max - m_bbox_lat_min;

    const double bs = baseScale();
    const double min_span = 0.001;
    if (dlon < min_span) dlon = min_span;
    if (dlat < min_span) dlat = min_span;

    const double zoom_x = (width()  * 0.70) / (dlon * cos_ref * bs);
    const double zoom_y = (height() * 0.70) / (dlat * bs);
    m_zoom = std::clamp(std::min(zoom_x, zoom_y), 1e-6, 1e8);

    const double cx = (m_bbox_lon_min + m_bbox_lon_max) * 0.5;
    const double cy = (m_bbox_lat_min + m_bbox_lat_max) * 0.5;
    const double sc = bs * m_zoom;
    m_origin = QPointF(-cx * cos_ref * sc, cy * sc);

    update();
}

void MapView::fitToLayer(const std::string& layer_id)
{
    const auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end()) return;

    const LayerMapData& ld = it->second;
    if (ld.lon_min > ld.lon_max || ld.lat_min > ld.lat_max) return;
    if (width() <= 0 || height() <= 0) { m_needs_fit = true; return; }

    m_ref_lat = (ld.lat_min + ld.lat_max) * 0.5;
    const double cos_ref = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_ref_lat * kDegToRad));

    double dlon = ld.lon_max - ld.lon_min;
    double dlat = ld.lat_max - ld.lat_min;

    const double bs = baseScale();
    const double min_span = 0.001;
    if (dlon < min_span) dlon = min_span;
    if (dlat < min_span) dlat = min_span;

    const double zoom_x = (width()  * 0.70) / (dlon * cos_ref * bs);
    const double zoom_y = (height() * 0.70) / (dlat * bs);
    m_zoom = std::clamp(std::min(zoom_x, zoom_y), 1e-6, 1e8);

    const double cx = (ld.lon_min + ld.lon_max) * 0.5;
    const double cy = (ld.lat_min + ld.lat_max) * 0.5;
    const double sc = bs * m_zoom;
    m_origin = QPointF(-cx * cos_ref * sc, cy * sc);
    m_user_interacted = true;

    update();
}

// ── Coordinate helpers ────────────────────────────────────────────────────────

QPointF MapView::geoToPixel(double lon, double lat) const
{
    const double sc      = baseScale() * m_zoom;
    const double cos_ref = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_ref_lat * kDegToRad));
    return QPointF(width()  / 2.0, height() / 2.0)
         + m_origin
         + QPointF(lon * cos_ref * sc, -lat * sc);
}

QPointF MapView::pixelToGeo(QPointF px) const
{
    const double sc      = baseScale() * m_zoom;
    const double cos_ref = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_ref_lat * kDegToRad));
    const QPointF cen(width() / 2.0, height() / 2.0);
    const double lon = (px.x() - cen.x() - m_origin.x()) / (sc * cos_ref);
    const double lat = -(px.y() - cen.y() - m_origin.y()) / sc;
    return {lon, lat};
}

// ── setLayerMapData ───────────────────────────────────────────────────────────
//
// Store a LayerMapData that was fully built on a background thread.
// Cheap: just stores the data and triggers a repaint.

void MapView::setLayerMapData(const std::string& layer_id, LayerMapData data)
{
    // On update (entry already exists): preserve user-controlled visibility.
    // On first insert: honor the incoming flags so callers can pre-set them.
    auto it = m_layer_data.find(layer_id);
    if (it != m_layer_data.end()) {
        data.visible        = it->second.visible;
        data.show_nav_track = it->second.show_nav_track;
        it->second = std::move(data);
    } else {
        it = m_layer_data.emplace(layer_id, std::move(data)).first;
    }
    LayerMapData& ld = it->second;

    rebuildCombined();
    // Only auto-fit for the first data arrival; once the user has panned or
    // zoomed m_user_interacted is true and background layer arrivals do not
    // yank the view.
    if (!m_user_interacted && !ld.nav_track.empty())
        fitToData();
    update();
    emit layerDataUpdated(layer_id);
}

const LayerMapData* MapView::layerData(const std::string& id) const
{
    const auto it = m_layer_data.find(id);
    return it != m_layer_data.end() ? &it->second : nullptr;
}

std::vector<std::string> MapView::layerDataIds() const
{
    std::vector<std::string> ids;
    ids.reserve(m_layer_data.size());
    for (const auto& [id, data] : m_layer_data)
        ids.push_back(id);
    return ids;
}

// ── updatePreviewImage ────────────────────────────────────────────────────────
//
// Replace only the preview image for an already-loaded layer.
// All other LayerMapData fields (coverage, nav track, bbox) are unchanged.
// This is O(1) on the map side — the heavy recolor work was done by the caller.

void MapView::updatePreviewImage(const std::string& layer_id, QImage img)
{
    const auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end()) return;
    it->second.preview_image = std::move(img);
    update();
    emit layerDataUpdated(layer_id);
}

// ── removeLayerData / setLayerVisible / clearAllLayerData ────────────────────

void MapView::removeLayerData(const std::string& layer_id)
{
    m_layer_data.erase(layer_id);
    rebuildCombined();
    update();
}

void MapView::setLayerVisible(const std::string& layer_id, bool visible)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end()) return;
    if (it->second.visible == visible) return;
    it->second.visible = visible;
    rebuildCombined();
    update();
}

void MapView::setSelectedLayers(const std::vector<std::string>& ids)
{
    m_selected_layer_ids = std::set<std::string>(ids.begin(), ids.end());
    update();
}

void MapView::setNavTrackVisible(const std::string& layer_id, bool visible)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end()) return;
    if (it->second.show_nav_track == visible) return;
    it->second.show_nav_track = visible;
    rebuildCombined();
    update();
}

bool MapView::isNavTrackVisible(const std::string& layer_id) const
{
    const auto it = m_layer_data.find(layer_id);
    return it != m_layer_data.end() && it->second.show_nav_track;
}

void MapView::clearAllLayerData()
{
    m_layer_data.clear();
    m_nav_track.clear();
    m_bbox_lon_min =  1e18; m_bbox_lon_max = -1e18;
    m_bbox_lat_min =  1e18; m_bbox_lat_max = -1e18;
    m_is_projected = false;
    update();
}

bool MapView::isLayerVisible(const std::string& id) const
{
    const auto it = m_layer_data.find(id);
    return it != m_layer_data.end() && it->second.visible;
}

QRectF MapView::layerPaintRect(const std::string& id) const
{
    const auto it = m_layer_data.find(id);
    if (it == m_layer_data.end() || it->second.preview_image.isNull()) return {};
    const auto& ld = it->second;
    return QRectF(geoToPixel(ld.lon_min, ld.lat_max),
                  geoToPixel(ld.lon_max, ld.lat_min));
}

} // namespace dolphin::ui
