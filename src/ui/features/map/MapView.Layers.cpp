// MapView.Layers.cpp — map-layer data and display-state management.

#include "ui/features/map/MapView.h"
#include "ui/features/map/MapDrapeHull.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace dolphin::ui {

void MapView::setLayerMapData(const std::string& layer_id, LayerMapData data)
{
    if (m_project) {
        if (const auto* l = m_project->findLayer(layer_id)) {
            data.visible       = l->visible;
            data.opacity       = l->map_opacity;
            data.blend_mode    = l->map_blend_mode;
            data.clip_polygons = l->map_clip_polygons;
            data.show_beams    = l->map_show_beams;
            data.beam_spacing  = l->map_beam_spacing;
        }
    }
    alignLayerLongitudeBranch(layer_id, data);
    if (!data.preview_image.isNull() && data.raster_boundary.empty())
        data.raster_boundary = buildSonarRasterBoundary(
            data.preview_image, data.lon_min, data.lat_min,
            data.lon_max, data.lat_max);
    auto it = m_layer_data.find(layer_id);
    if (it != m_layer_data.end()) {
        data.visible        = it->second.visible;
        data.show_nav_track = it->second.show_nav_track;
        data.show_nadir     = it->second.show_nadir;
        it->second = std::move(data);
    } else {
        it = m_layer_data.emplace(layer_id, std::move(data)).first;
    }
    LayerMapData& ld = it->second;

    m_combined_dirty = true;
    if (!ld.nav_track.empty() && (m_frame_survey_pending || !m_user_interacted))
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

void MapView::updatePreviewImage(const std::string& layer_id, QImage img,
                                 const SonarDisplayParams* gpu_params)
{
    const auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end()) return;
    it->second.preview_image = std::move(img);
    if (gpu_params) it->second.gpu_display_params = *gpu_params;
    update();
    emit layerDataUpdated(layer_id);
}

void MapView::removeLayerData(const std::string& layer_id)
{
    m_layer_data.erase(layer_id);
    m_combined_dirty = true;
    update();
}

void MapView::setLayerVisible(const std::string& layer_id, bool visible)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.visible == visible) return;
    it->second.visible = visible;
    m_combined_dirty = true;
    update();
}

void MapView::setLayerOpacity(const std::string& layer_id, float opacity)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || std::abs(it->second.opacity - opacity) < 1e-3f) return;
    it->second.opacity = opacity;
    update();
}

void MapView::setLayerBlendMode(const std::string& layer_id, int blend_mode)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.blend_mode == blend_mode) return;
    it->second.blend_mode = blend_mode;
    update();
}

void MapView::setLayerClipPolygons(const std::string& layer_id, bool clip)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.clip_polygons == clip) return;
    it->second.clip_polygons = clip;
    update();
}

void MapView::setLayerShowBeams(const std::string& layer_id, bool show)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.show_beams == show) return;
    it->second.show_beams = show;
    update();
}

void MapView::setLayerBeamSpacing(const std::string& layer_id, int spacing)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.beam_spacing == spacing) return;
    it->second.beam_spacing = spacing;
    update();
}

void MapView::setLayerShowNadir(const std::string& layer_id, bool show)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.show_nadir == show) return;
    it->second.show_nadir = show;
    update();
    emit layerDataUpdated(layer_id);
}

void MapView::setSelectedLayers(const std::vector<std::string>& ids)
{
    m_selected_layer_ids = std::set<std::string>(ids.begin(), ids.end());
    update();
}

void MapView::setNavTrackVisible(const std::string& layer_id, bool visible)
{
    auto it = m_layer_data.find(layer_id);
    if (it == m_layer_data.end() || it->second.show_nav_track == visible) return;
    it->second.show_nav_track = visible;
    m_combined_dirty = true;
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
    m_lon_branch_ref = 0.0;
    m_has_lon_branch_ref = false;
    m_is_projected = false;
    m_combined_dirty = false;
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
