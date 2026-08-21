// MapView3D.cpp — data API, camera, and coordinate helpers.
//
// GL setup + geometry builders:  MapView3D.GL.cpp
// Terrain file loading:          MapView3D.Load.cpp
// Paint phases:                  MapView3D.Paint.cpp
// Mouse / wheel input:           MapView3D.Input.cpp

#include "ui/features/map/MapView3D.h"

#include <QOpenGLTexture>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Construction / destruction
// -----------------------------------------------------------------------------

MapView3D::MapView3D(QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent)
{
    // QWindow has no size policy / auto-fill / mouse-tracking / paint attributes —
    // the container widget governs sizing, and a QWindow always receives mouse moves.
    setMinimumSize(QSize(200, 200));
    setCursor(Qt::OpenHandCursor);

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setAlphaBufferSize(0);
    fmt.setStencilBufferSize(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSamples(0);
    setFormat(fmt);
}

MapView3D::~MapView3D()
{
    if (m_gl_ready && context() && context()->isValid()) {
        makeCurrent();
        m_grid_quad_vbo.destroy();
        m_survey_vbo.destroy();
        m_nav_merged_vbo.destroy();
        for (auto& C : m_curtain_layers) { C.vbo.destroy(); delete C.texture; C.texture = nullptr; }
        for (auto& T : m_terrain_layers) { T.vbo.destroy(); T.vbo_lod.destroy(); }
        for (auto& D : m_drape_layers) {
            delete D.texture;
            D.quad_vbo.destroy();
            D.outline_vbo.destroy();
        }
        delete m_drape_palette_texture;
        m_drape_palette_texture = nullptr;
        m_vao.destroy();
    }
    delete m_shader;
    delete m_grid_shader;
    delete m_curtain_shader;
    delete m_terrain_shader;
    delete m_drape_shader;
    delete m_drape_palette_texture;
    m_drape_palette_texture = nullptr;
    doneCurrent();
}

// -----------------------------------------------------------------------------
//  Scene origin
// -----------------------------------------------------------------------------

void MapView3D::setSceneOrigin(double x, double y, bool is_projected)
{
    m_origin_x     = x;
    m_origin_y     = y;
    m_is_projected = is_projected;
    m_has_origin   = true;
}

// -----------------------------------------------------------------------------
//  Nav-track API
// -----------------------------------------------------------------------------

void MapView3D::updateNavTrack(const std::string& layer_id,
                                const LayerMapData& data,
                                QColor color)
{
    if (!m_has_origin) return;

    auto it = std::find_if(m_layers.begin(), m_layers.end(),
                           [&](const NavLayer3D& L){ return L.id == layer_id; });
    if (it == m_layers.end()) {
        m_layers.push_back(NavLayer3D{});
        it = m_layers.end() - 1;
        it->id = layer_id;
    }
    it->color     = color;
    it->raw_track = data.nav_track;
    it->dirty     = true;
    m_layers_dirty = true;

    // Expand scene radius from bounding box; store local bbox for survey outline.
    if (data.lon_min < data.lon_max && data.lat_min < data.lat_max) {
        const QVector3D lo = toLocal(data.lon_min, data.lat_min);
        const QVector3D hi = toLocal(data.lon_max, data.lat_max);
        it->bbox_xmin = lo.x();  it->bbox_xmax = hi.x();
        it->bbox_ymin = lo.y();  it->bbox_ymax = hi.y();
        it->has_bbox  = true;
        m_survey_dirty = true;
        const float r = QVector3D(hi - lo).length() * 0.5f;
        if (r > m_scene_radius) {
            m_scene_radius = r;
            if (!m_camera_user_moved) fitToScene();
        }
    }

    update();
}

void MapView3D::removeLayer(const std::string& layer_id)
{
    auto it = std::find_if(m_layers.begin(), m_layers.end(),
                           [&](const NavLayer3D& L){ return L.id == layer_id; });
    if (it == m_layers.end()) return;
    m_layers.erase(it);
    m_survey_dirty  = true;
    m_layers_dirty  = true;   // trigger merged VBO rebuild without the removed layer
    update();
}

// -----------------------------------------------------------------------------
//  Sonar drape API (Phase 3)
// -----------------------------------------------------------------------------

void MapView3D::setSonarDrape(const std::string& layer_id,
                               const QImage& image,
                               const QImage& intensity_image,
                               const SonarDisplayParams& display_params,
                               double lon_min, double lat_min,
                               double lon_max, double lat_max,
                               std::vector<QPointF> hull_geo,
                               float opacity)
{
    if (!m_has_origin) return;
    if ((image.isNull() && intensity_image.isNull())
            || lon_min >= lon_max || lat_min >= lat_max) return;

    const QVector3D sw = toLocal(lon_min, lat_min);
    const QVector3D ne = toLocal(lon_max, lat_max);

    auto it = std::find_if(m_drape_layers.begin(), m_drape_layers.end(),
                           [&](const SonarDrape3D& D){ return D.id == layer_id; });
    if (it == m_drape_layers.end()) {
        m_drape_layers.push_back(SonarDrape3D{});
        it = m_drape_layers.end() - 1;
        it->id      = layer_id;
        it->opacity = opacity;   // seed once; live changes go through setLayerOpacity
    }

    const bool use_raw = !intensity_image.isNull();
    const QImage& source = use_raw ? intensity_image : image;
    const quint64 source_key = source.cacheKey();
    if (it->source_key != source_key || it->raw_intensity != use_raw) {
        // GL tex row 0 = south (low Y), QImage row 0 = north (high Y).
        it->pending_image = use_raw
            ? source.mirrored()
            : source.convertToFormat(QImage::Format_RGBA8888).mirrored();
        it->pending_fallback_image = use_raw
            ? image.convertToFormat(QImage::Format_RGBA8888).mirrored()
            : QImage{};
        it->source_key = source_key;
        it->raw_intensity = use_raw;
        it->dirty = true;
        m_drapes_dirty = true;
    }
    it->display_params = display_params;
    const bool geometry_changed = it->bbox_x0 != sw.x() || it->bbox_y0 != sw.y()
        || it->bbox_w != ne.x() - sw.x() || it->bbox_h != ne.y() - sw.y();
    if (geometry_changed || it->outline_vert_count == 0) {
        it->pending_hull = std::move(hull_geo);
        it->dirty = true;
        m_drapes_dirty = true;
    }
    it->bbox_x0 = sw.x();
    it->bbox_y0 = sw.y();
    it->bbox_w  = ne.x() - sw.x();
    it->bbox_h  = ne.y() - sw.y();
    update();
}

void MapView3D::setSonarPalette(int palette_index)
{
    palette_index = std::clamp(palette_index, 0, PaletteIndex::Count - 1);
    if (m_sonar_palette == palette_index) return;
    m_sonar_palette = palette_index;
    m_drape_palette_dirty = true;
    m_drapes_dirty = true;
    update();
}

void MapView3D::removeSonarDrape(const std::string& layer_id)
{
    auto it = std::find_if(m_drape_layers.begin(), m_drape_layers.end(),
                           [&](const SonarDrape3D& D){ return D.id == layer_id; });
    if (it == m_drape_layers.end()) return;
    if (m_gl_ready && context() && context()->isValid()) {
        makeCurrent();
        delete it->texture;
        it->texture = nullptr;
        it->quad_vbo.destroy();
        it->outline_vbo.destroy();
        doneCurrent();
    } else {
        delete it->texture;
        it->texture = nullptr;
    }
    m_drape_layers.erase(it);
    update();
}

// -----------------------------------------------------------------------------
//  Profile curtain API (Phase 2)
// -----------------------------------------------------------------------------

void MapView3D::setProfileCurtain(const std::string& layer_id, const LayerMapData& data)
{
    if (!m_has_origin) return;
    // The curtain is the REAL profile raster; without it there is nothing
    // honest to show (the old per-ping amplitude smear misrepresented the data).
    if (data.nav_track.empty() || data.curtain_image.isNull()
            || data.curtain_depth_m <= 0.f)
        return;

    auto it = std::find_if(m_curtain_layers.begin(), m_curtain_layers.end(),
                           [&](const CurtainLayer3D& C){ return C.id == layer_id; });
    if (it == m_curtain_layers.end()) {
        m_curtain_layers.push_back(CurtainLayer3D{});
        it = m_curtain_layers.end() - 1;
        it->id      = layer_id;
        it->opacity = data.opacity;   // seed once; live changes via setLayerOpacity
    }

    const size_t n       = data.nav_track.size();
    const float  depth_m = data.curtain_depth_m;
    const float  u_scale = n > 1 ? 1.f / static_cast<float>(n - 1) : 1.f;

    std::vector<float> verts;
    verts.reserve(n * 30);  // 6 vertices × 5 floats (xyz + uv) per segment

    float xmin =  std::numeric_limits<float>::max();
    float xmax = -std::numeric_limits<float>::max();
    float ymin =  std::numeric_limits<float>::max();
    float ymax = -std::numeric_limits<float>::max();
    bool  has_bbox = false;

    for (size_t i = 0; i + 1 < n; ++i) {
        const QPointF& ga = data.nav_track[i];
        const QPointF& gb = data.nav_track[i + 1];
        if (std::isnan(ga.x()) || std::isnan(gb.x())) continue;

        const float u_a = static_cast<float>(i)     * u_scale;
        const float u_b = static_cast<float>(i + 1) * u_scale;

        // XY in local metres; Z is negative depth (not VE-scaled — shader handles VE).
        // The quad spans the FULL profile depth; rows without data are
        // transparent in the texture (shader discards), so the bottom edge
        // follows the real data extent.
        const QVector3D pa = toLocal(ga.x(), ga.y(), 0.0);
        const QVector3D pb = toLocal(gb.x(), gb.y(), 0.0);

        auto push5 = [&](float x, float y, float z, float u, float v) {
            verts.push_back(x); verts.push_back(y); verts.push_back(z);
            verts.push_back(u); verts.push_back(v);
        };
        push5(pa.x(), pa.y(), -depth_m, u_a, 1.f);
        push5(pa.x(), pa.y(), 0.f,      u_a, 0.f);
        push5(pb.x(), pb.y(), -depth_m, u_b, 1.f);
        push5(pa.x(), pa.y(), 0.f,      u_a, 0.f);
        push5(pb.x(), pb.y(), 0.f,      u_b, 0.f);
        push5(pb.x(), pb.y(), -depth_m, u_b, 1.f);

        xmin = std::min({xmin, pa.x(), pb.x()});
        xmax = std::max({xmax, pa.x(), pb.x()});
        ymin = std::min({ymin, pa.y(), pb.y()});
        ymax = std::max({ymax, pa.y(), pb.y()});
        has_bbox = true;
    }

    it->cpu_verts     = std::move(verts);
    it->pending_image = data.curtain_image;
    it->z_range       = depth_m;
    it->bbox_xmin     = xmin;  it->bbox_xmax = xmax;
    it->bbox_ymin     = ymin;  it->bbox_ymax = ymax;
    it->dirty         = true;
    m_curtains_dirty  = true;
    m_survey_dirty    = true;

    if (has_bbox) {
        const float r = 0.5f * std::sqrt(
            (xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin));
        if (r > m_scene_radius) {
            m_scene_radius = r;
            if (!m_camera_user_moved) fitToScene();
        }
    }

    update();
}

void MapView3D::removeProfileCurtain(const std::string& layer_id)
{
    auto it = std::find_if(m_curtain_layers.begin(), m_curtain_layers.end(),
                           [&](const CurtainLayer3D& C){ return C.id == layer_id; });
    if (it == m_curtain_layers.end()) return;
    if (m_gl_ready) {
        if (!context() || !context()->isValid()) {
            m_curtain_layers.erase(it);
            m_survey_dirty = true;
            return;
        }
        makeCurrent();
        it->vbo.destroy();
        delete it->texture;
        it->texture = nullptr;
        doneCurrent();
    }
    m_curtain_layers.erase(it);
    m_survey_dirty = true;
    update();
}

void MapView3D::setCurtainPalette(int palette_index)
{
    if (m_curtain_palette == palette_index) return;
    m_curtain_palette = palette_index;   // shader uniform — no rebuild needed
    update();
}

// -----------------------------------------------------------------------------
//  Scene / display
// -----------------------------------------------------------------------------

void MapView3D::clearScene()
{
    m_terrain_load_generation.clear();
    m_pending_terrain_loads.clear();
    m_terrain_loading = false;
    if (m_gl_ready && context() && context()->isValid()) {
        makeCurrent();
        m_nav_merged_vbo.destroy();
        for (auto& C : m_curtain_layers) { C.vbo.destroy(); delete C.texture; C.texture = nullptr; }
        for (auto& T : m_terrain_layers) { T.vbo.destroy(); T.vbo_lod.destroy(); }
        for (auto& D : m_drape_layers) {
            delete D.texture;
            D.quad_vbo.destroy();
            D.outline_vbo.destroy();
        }
        doneCurrent();
    }
    m_layers.clear();
    m_curtain_layers.clear();
    m_terrain_layers.clear();
    m_drape_layers.clear();
    m_nav_merged_count = 0;
    m_has_origin       = false;
    m_scene_radius     = 500.f;
    m_curtains_dirty   = false;
    m_terrain_dirty    = false;
    m_layers_dirty     = false;
    m_drapes_dirty     = false;
    m_survey_count     = 0;
    m_survey_dirty     = false;
    resetCamera();
    update();
}

void MapView3D::setVerticalExaggeration(float ve)
{
    m_v_exag       = qMax(0.1f, ve);
    m_layers_dirty = true;   // nav VBOs use toLocal() which applies m_v_exag
    update();
}

void MapView3D::setShowGrid(bool show)
{
    if (m_show_grid == show) return;
    m_show_grid = show;
    update();
}

void MapView3D::setMapBgColor(QColor c)
{
    if (m_map_bg_color == c) return;
    m_map_bg_color = c;
    update();
}

void MapView3D::setGridColors(QColor minor, QColor major)
{
    if (m_grid_minor_color == minor && m_grid_major_color == major) return;
    m_grid_minor_color = minor;
    m_grid_major_color = major;
    update();
}

void MapView3D::setGratLabelSize(int size)
{
    if (m_grat_label_size == size) return;
    m_grat_label_size = size;
    update();
}

void MapView3D::setGratLabelRotated(bool rotated)
{
    if (m_grat_label_rotated == rotated) return;
    m_grat_label_rotated = rotated;
    update();
}

void MapView3D::setGratCoordFormat(int fmt)
{
    if (m_grat_coord_fmt == fmt) return;
    m_grat_coord_fmt = fmt;
    update();
}

void MapView3D::setLayerVisible(const std::string& layer_id, bool visible)
{
    bool changed = false;
    for (auto& L : m_layers)
        if (L.id == layer_id && L.layer_visible != visible) {
            L.layer_visible = visible; changed = true;
        }
    for (auto& C : m_curtain_layers)
        if (C.id == layer_id && C.visible != visible) { C.visible = visible; changed = true; }
    for (auto& T : m_terrain_layers)
        if (T.id == layer_id && T.visible != visible) { T.visible = visible; changed = true; }
    for (auto& D : m_drape_layers)
        if (D.id == layer_id && D.visible != visible) { D.visible = visible; changed = true; }
    if (changed) {
        m_survey_dirty = true;
        update();
    }
}

void MapView3D::setNavTrackVisible(const std::string& layer_id, bool visible)
{
    for (auto& layer : m_layers) {
        if (layer.id != layer_id || layer.nav_visible == visible) continue;
        layer.nav_visible = visible;
        update();
        return;
    }
}

void MapView3D::setLayerOpacity(const std::string& layer_id, float opacity)
{
    bool changed = false;
    for (auto& D : m_drape_layers)
        if (D.id == layer_id && std::abs(D.opacity - opacity) > 1e-3f) {
            D.opacity = opacity;
            changed = true;
        }
    // SBP layers present on the map as curtains, not drapes — fade those too.
    for (auto& C : m_curtain_layers)
        if (C.id == layer_id && std::abs(C.opacity - opacity) > 1e-3f) {
            C.opacity = opacity;
            changed = true;
        }
    if (changed) update();
}

void MapView3D::setActiveLayer(const std::string& layer_id)
{
    if (m_active_layer_id == layer_id) return;
    m_active_layer_id = layer_id;
    update();
}

void MapView3D::setSelectedLayers(const std::vector<std::string>& ids)
{
    if (m_selected_layer_ids == ids) return;
    m_selected_layer_ids = ids;
    update();
}

// -----------------------------------------------------------------------------
//  Camera
// -----------------------------------------------------------------------------

void MapView3D::resetCamera()
{
    m_camera.target      = {0.f, 0.f, 0.f};
    m_camera.distance    = m_scene_radius * 2.5f;
    m_camera.yaw         = 0.f;
    m_camera.pitch       = 45.f;
    m_camera_user_moved  = false;
    update();
}

void MapView3D::fitToScene()
{
    m_camera.target      = {0.f, 0.f, 0.f};
    m_camera.distance    = m_scene_radius / std::tan(m_camera.fov * float(M_PI) / 360.f) * 1.3f;
    m_camera.distance    = qMax(m_camera.distance, 10.f);
    m_camera_user_moved  = false;
    update();
}

// -----------------------------------------------------------------------------
//  Coordinate helper (used by data API + geometry builders)
// -----------------------------------------------------------------------------

QVector3D MapView3D::toLocal(double x, double y, double z) const
{
    double lx, ly;
    if (m_is_projected) {
        lx = x - m_origin_x;
        ly = y - m_origin_y;
    } else {
        static constexpr double kMetPerDeg = 111320.0;
        const double cos_lat = std::cos(m_origin_y * M_PI / 180.0);
        lx = (x - m_origin_x) * cos_lat * kMetPerDeg;
        ly = (y - m_origin_y)            * kMetPerDeg;
    }
    return QVector3D(static_cast<float>(lx),
                     static_cast<float>(ly),
                     static_cast<float>(z * m_v_exag));
}

} // namespace dolphin::ui
