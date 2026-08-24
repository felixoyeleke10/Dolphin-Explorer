// MapView.cpp — data API, coordinate helpers, nav-track assembly.
//
// Painting:    MapViewPaint.cpp           (paintEvent)
// Input:       MapViewInput.cpp           (resizeEvent, mouse/wheel/leave events)
// Swath build (runs off UI thread): SssMapBuild.cpp

#include "ui/features/map/MapView.h"
#include "ui/features/map/MapLongitude.h"
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
#include <optional>
#include <utility>
#include <vector>

namespace dolphin::ui {

// -- Constants -----------------------------------------------------------------

static constexpr double kDegToRad  = M_PI / 180.0;
static constexpr double kPixPerDeg = 6.0;
static constexpr double kPixPerM   = 0.5;

// -- Constructor ---------------------------------------------------------------

MapView::MapView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // ClickFocus lets the feature-draw tool receive Enter/Esc/Backspace keys.
    setFocusPolicy(Qt::ClickFocus);
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

// -- Data API ------------------------------------------------------------------

void MapView::setProject(app::Project* project)
{
    m_project = project;
    m_active_layer_id.clear();
    m_layer_data.clear();
    m_nav_track.clear();
    m_needs_fit       = false;
    m_user_interacted = false;
    m_lon_branch_ref  = 0.0;
    m_has_lon_branch_ref = false;
    m_bbox_lon_min =  1e18; m_bbox_lon_max = -1e18;
    m_bbox_lat_min =  1e18; m_bbox_lat_max = -1e18;
    update();
}

void MapView::setActiveLayer(const std::string& layer_id)
{
    m_active_layer_id = layer_id;
    rebuildNavTrack();
}

void MapView::alignLayerLongitudeBranch(const std::string& layer_id,
                                        LayerMapData& data) const
{
    if (data.is_projected || !maplongitude::validBounds(data))
        return;

    std::optional<double> reference;

    // A rebuild of an existing layer must remain on its previous branch even if
    // every other layer is currently hidden or still loading.
    if (const auto same = m_layer_data.find(layer_id);
            same != m_layer_data.end() && !same->second.is_projected
            && maplongitude::validBounds(same->second)) {
        reference = maplongitude::boundsCenter(same->second);
    } else if (m_has_lon_branch_ref) {
        // fitToExtent may establish the branch before decoded layer data arrives.
        reference = m_lon_branch_ref;
    } else {
        // All stored geographic layers are aligned on insertion, so any valid
        // one is a stable reference for the incoming layer.
        for (const auto& [id, existing] : m_layer_data) {
            if (id == layer_id || existing.is_projected
                    || !maplongitude::validBounds(existing))
                continue;
            reference = maplongitude::boundsCenter(existing);
            break;
        }
    }

    if (reference)
        maplongitude::alignLayerToReference(data, *reference);
}

void MapView::refreshLayerOrder()
{
    m_combined_dirty = true;
    update();
}

double MapView::baseScale() const
{
    return m_is_projected ? kPixPerM : kPixPerDeg;
}

double MapView::viewportMetresPerPixel() const
{
    const double sc = baseScale() * m_zoom;
    return (m_is_projected ? 1.0 : 111320.0) / sc;
}

void MapView::setSelectedContact(uint64_t id)
{
    if (m_selected_contact_id == id) return;
    m_selected_contact_id = id;
    update();
}

// -- rebuildNavTrack -----------------------------------------------------------

void MapView::rebuildNavTrack()
{
    if (m_active_layer_id.empty() || !m_project) {
        m_combined_dirty = true;
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
            m_combined_dirty = true;
            update();
            return;
        }
    }

    const app::DataLayer* layer = m_project->findLayer(m_active_layer_id);
    if (!layer || !layer->index_built || layer->artifact_index.empty()) {
        m_combined_dirty = true;
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
    alignLayerLongitudeBranch(m_active_layer_id, built);

    // Preserve view-state that lives on the main thread.
    LayerMapData& ld        = m_layer_data[m_active_layer_id];
    const bool visible      = ld.visible;
    const bool show_nav     = ld.show_nav_track;
    ld                      = std::move(built);
    ld.visible              = visible;
    ld.show_nav_track       = show_nav;

    m_combined_dirty = true;
    if (ld.track_stats.track_points > 0)
        fitToData();  // ensureCombined() called inside fitToData()
    update();
    emit layerDataUpdated(m_active_layer_id);
}

// -- ensureCombined / rebuildCombined -----------------------------------------

void MapView::ensureCombined()
{
    if (!m_combined_dirty) return;
    rebuildCombined();
    m_combined_dirty = false;
}

void MapView::rebuildCombined()
{
    m_nav_track.clear();
    m_bbox_lon_min =  1e18; m_bbox_lon_max = -1e18;
    m_bbox_lat_min =  1e18; m_bbox_lat_max = -1e18;
    m_lon_branch_ref = 0.0;
    m_has_lon_branch_ref = false;
    m_is_projected = false;
    bool first_layer_seen = false;

    auto appendLayerData = [&](const LayerMapData& data) {
        if (data.show_nav_track && !data.nav_track.empty()) {
            if (!m_nav_track.empty())
                m_nav_track.push_back({std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::quiet_NaN()});
            // Display decimation: the combined track is repainted on every
            // pan/zoom frame, and SBP profile tracks carry one point per trace
            // (tens of thousands). Cap each layer's contribution; NaN segment
            // breaks and the final point always survive. Full-resolution data
            // stays in LayerMapData for hit-testing / 3D / stats.
            constexpr size_t kMaxTrackPts = 3000;
            const size_t n      = data.nav_track.size();
            const size_t stride = std::max<size_t>(1, n / kMaxTrackPts);
            if (stride == 1) {
                m_nav_track.insert(m_nav_track.end(),
                                   data.nav_track.begin(), data.nav_track.end());
            } else {
                for (size_t i = 0; i < n; ++i) {
                    const QPointF& pt = data.nav_track[i];
                    if (std::isnan(pt.x()) || i % stride == 0 || i + 1 == n)
                        m_nav_track.push_back(pt);
                }
            }
        }
        double data_lon_min = data.lon_min;
        double data_lon_max = data.lon_max;
        if (!data.is_projected && maplongitude::validBounds(data)) {
            const double centre = maplongitude::boundsCenter(data);
            if (!m_has_lon_branch_ref) {
                m_lon_branch_ref = centre;
                m_has_lon_branch_ref = true;
            } else {
                const double delta = maplongitude::branchShift(
                    centre, m_lon_branch_ref);
                data_lon_min += delta;
                data_lon_max += delta;
            }
        }
        m_bbox_lon_min = std::min(m_bbox_lon_min, data_lon_min);
        m_bbox_lon_max = std::max(m_bbox_lon_max, data_lon_max);
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

// -- fitToData / fitToDataAndReset ---------------------------------------------

void MapView::fitToDataAndReset()
{
    m_user_interacted = false;
    fitToData();
}

void MapView::fitToData()
{
    ensureCombined();
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
    if (!m_is_projected) {
        m_lon_branch_ref = cx;
        m_has_lon_branch_ref = true;
    }
    const double sc = bs * m_zoom;
    m_origin = QPointF(-cx * cos_ref * sc, cy * sc);

    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

void MapView::fitToExtent(double lon_min, double lon_max,
                           double lat_min, double lat_max, bool is_projected)
{
    if (m_user_interacted) return;  // preserve user-positioned viewport
    if (lon_min > lon_max || lat_min > lat_max) return;
    if (width() <= 0 || height() <= 0) { m_needs_fit = true; return; }

    if (!is_projected) {
        const auto interval = maplongitude::shortGeographicInterval(
            lon_min, lon_max);
        lon_min = interval.min;
        lon_max = interval.max;
        if (m_has_lon_branch_ref) {
            const double centre = lon_min + (lon_max - lon_min) * 0.5;
            const double delta = maplongitude::branchShift(
                centre, m_lon_branch_ref);
            lon_min += delta;
            lon_max += delta;
        }
        m_lon_branch_ref = lon_min + (lon_max - lon_min) * 0.5;
        m_has_lon_branch_ref = true;
    } else {
        m_lon_branch_ref = 0.0;
        m_has_lon_branch_ref = false;
    }

    m_is_projected = is_projected;
    m_ref_lat = (lat_min + lat_max) * 0.5;
    const double cos_ref = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_ref_lat * kDegToRad));

    const double bs = baseScale();
    const double dlon = std::max(lon_max - lon_min, 0.001);
    const double dlat = std::max(lat_max - lat_min, 0.001);
    const double zoom_x = (width()  * 0.70) / (dlon * cos_ref * bs);
    const double zoom_y = (height() * 0.70) / (dlat * bs);
    m_zoom = std::clamp(std::min(zoom_x, zoom_y), 1e-6, 1e8);

    const double cx = (lon_min + lon_max) * 0.5;
    const double cy = (lat_min + lat_max) * 0.5;
    const double sc = bs * m_zoom;
    m_origin = QPointF(-cx * cos_ref * sc, cy * sc);
    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
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
    if (!ld.is_projected) {
        m_lon_branch_ref = cx;
        m_has_lon_branch_ref = true;
    }
    const double sc = bs * m_zoom;
    m_origin = QPointF(-cx * cos_ref * sc, cy * sc);
    m_user_interacted = true;
    m_frame_survey_pending = false;  // explicit single-layer fit overrides survey framing

    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

// -- Programmatic viewport control --------------------------------------------

void MapView::setZoomFromMpp(double mpp)
{
    if (mpp <= 0.0 || std::isnan(mpp)) return;
    const double metresPerUnit = m_is_projected ? 1.0 : 111320.0;
    const double newZoom = std::clamp(metresPerUnit / (baseScale() * mpp), 1e-6, 1e8);
    // Scale origin by the zoom ratio so the viewport centre stays fixed on the
    // same geographic position — identical to wheel-zoom from the centre pixel.
    m_origin *= (newZoom / m_zoom);
    m_zoom    = newZoom;
    m_user_interacted = true;
    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

void MapView::panByPixels(int dx, int dy)
{
    m_origin += QPointF(dx, dy);
    m_user_interacted = true;
    m_frame_survey_pending = false;
    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

void MapView::setRotationDeg(double deg)
{
    m_rotation_deg = std::fmod(deg, 360.0);
    if (m_rotation_deg < 0.0) m_rotation_deg += 360.0;
    update();
    emit viewportChanged(viewportMetresPerPixel(), m_rotation_deg);
}

// -- Coordinate helpers --------------------------------------------------------

QPointF MapView::geoToPixel(double lon, double lat) const
{
    if (!m_is_projected && m_has_lon_branch_ref)
        lon = maplongitude::unwrapNear(lon, m_lon_branch_ref);
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

} // namespace dolphin::ui
