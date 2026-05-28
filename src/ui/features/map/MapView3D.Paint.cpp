// MapView3D.Paint.cpp — paintGL and GL draw phases.
// QPainter HUD overlays (drawHUD, drawGridLabels, drawScaleBar3D,
// drawCompassRose) live in MapView3D.Overlays.cpp.

#include "ui/features/map/MapView3D.h"
#include "ui/shell/Theme.h"

#include <QMatrix4x4>
#include <QOpenGLTexture>
#include <QPainter>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const QColor kSurveyOutline   ( 96, 176, 255, 160);
const QColor kNavLayerDefault (  0, 180, 255      );
const QColor kDrapeOutline    ( 59, 127, 255, 255);
} // namespace

namespace dolphin::ui {

void MapView3D::paintGL()
{
    if (!m_gl_ready) return;

    // ── FPS tracking (60-frame rolling average) ───────────────────────────────
    constexpr int kFpsWindow = 60;
    ++m_fps_frames;
    if (m_fps_frames >= kFpsWindow) {
        const qint64 ms = m_fps_timer.restart();
        m_fps_avg = ms > 0 ? float(kFpsWindow) * 1000.f / float(ms) : 0.f;
        m_fps_frames = 0;
    }

    // ── Upload any pending VBOs / textures ────────────────────────────────────
    if (m_layers_dirty)   { buildNavMergedVbo();      m_layers_dirty   = false; }
    if (m_curtains_dirty) { rebuildAllCurtainVbos(); m_curtains_dirty = false; }
    if (m_terrain_dirty)  { rebuildAllTerrainVbos(); m_terrain_dirty  = false; }
    if (m_drapes_dirty)   { uploadPendingDrapes();   m_drapes_dirty   = false; }
    if (m_survey_dirty)   { buildSurveyOutline();    m_survey_dirty   = false; }

    glClearColor(m_map_bg_color.redF(),
                 m_map_bg_color.greenF(),
                 m_map_bg_color.blueF(),
                 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_vao.bind();

    // ── 1. Infinite reference grid ─────────────────────────────────────────────
    if (m_show_grid && m_grid_shader && m_has_origin) {
        glDisable(GL_DEPTH_TEST);
        m_grid_shader->bind();
        drawGrid();
        m_grid_shader->release();
        glEnable(GL_DEPTH_TEST);
    }

    const QMatrix4x4 mvp = m_camera.mvp();

    // ── 2. Terrain ────────────────────────────────────────────────────────────
    if (m_terrain_shader && !m_terrain_layers.empty()) {
        m_terrain_shader->bind();
        m_terrain_shader->setUniformValue(m_loc_terr_mvp, mvp);
        drawTerrain();
        m_terrain_shader->release();
    }

    // ── 3. SBP curtain ────────────────────────────────────────────────────────
    if (m_curtain_shader && !m_curtain_layers.empty()) {
        m_curtain_shader->bind();
        m_curtain_shader->setUniformValue(m_loc_curt_mvp, mvp);
        drawCurtains();
        m_curtain_shader->release();
    }

    // ── 4. Sonar drape ────────────────────────────────────────────────────────
    if (m_drape_shader && !m_drape_layers.empty()) {
        m_drape_shader->bind();
        m_drape_shader->setUniformValue(m_loc_drape_mvp, mvp);
        drawDrapes();
        m_drape_shader->release();
    }

    // ── 5. Flat-colour overlay: survey outline, nav tracks, selection hull ───
    m_shader->bind();
    m_shader->setUniformValue(m_loc_mvp, mvp);
    drawSurveyOutline();
    drawNavLayers();
    drawDrapeOutlines();
    m_shader->release();

    m_vao.release();

    // ── HUD (QPainter overlay, implemented in MapView3D.Overlays.cpp) ─────────
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawHUD(painter);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw helpers
// ─────────────────────────────────────────────────────────────────────────────

void MapView3D::drawLines(QOpenGLBuffer& vbo, int count,
                          const QColor& col, float lw)
{
    if (count <= 0 || !vbo.isCreated()) return;
    vbo.bind();
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    m_shader->setUniformValue(m_loc_color,
        QVector4D(col.redF(), col.greenF(), col.blueF(), col.alphaF()));
    glLineWidth(lw);
    glDrawArrays(GL_LINES, 0, count);
    vbo.release();
}

void MapView3D::drawGrid()
{
    if (!m_grid_quad_vbo.isCreated()) return;

    const float dist = m_camera.distance;
    const float r    = std::max(m_scene_radius * 2.0f, dist * 4.0f);
    float step = std::pow(10.f, std::floor(std::log10(std::max(r / 4.f, 1.f))));
    while (r / step > 12.f) step *= 2.f;
    while (r / step < 4.f)  step /= 2.f;
    step = std::max(step, 0.1f);

    bool invOk = false;
    const QMatrix4x4 invVP = (m_camera.projMatrix() * m_camera.viewMatrix()).inverted(&invOk);
    if (!invOk) return;

    m_grid_shader->setUniformValue(m_loc_grid_invvp, invVP);
    m_grid_shader->setUniformValue(m_loc_grid_camxy,
        QVector2D(m_camera.target.x(), m_camera.target.y()));
    m_grid_shader->setUniformValue(m_loc_grid_minorstep, step);
    m_grid_shader->setUniformValue(m_loc_grid_majorstep, step * 10.f);
    m_grid_shader->setUniformValue(m_loc_grid_fadenear,  dist * Theme::kGrid3DFadeNearMult);
    m_grid_shader->setUniformValue(m_loc_grid_fadefar,   dist * Theme::kGrid3DFadeFarMult);
    m_grid_shader->setUniformValue(m_loc_grid_minorcol,
        QVector3D(m_grid_minor_color.redF(), m_grid_minor_color.greenF(), m_grid_minor_color.blueF()));
    m_grid_shader->setUniformValue(m_loc_grid_majorcol,
        QVector3D(m_grid_major_color.redF(), m_grid_major_color.greenF(), m_grid_major_color.blueF()));

    m_grid_quad_vbo.bind();
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_grid_quad_vbo.release();
}

void MapView3D::drawSurveyOutline()
{
    if (m_survey_count <= 0) return;
    // Hide when any layer is selected — the per-drape outline takes over.
    if (!m_active_layer_id.empty() || !m_selected_layer_ids.empty()) return;
    drawLines(m_survey_vbo, m_survey_count, kSurveyOutline, Theme::kSurveyOutlineWidth);
}

void MapView3D::drawNavLayers()
{
    if (m_nav_merged_count <= 0 || !m_nav_merged_vbo.isCreated()) return;

    const bool has_active    = !m_active_layer_id.empty();
    const bool has_selected  = !m_selected_layer_ids.empty();
    const bool any_highlight = has_active || has_selected;

    auto isHighlighted = [&](const std::string& id) -> bool {
        if (has_active && id == m_active_layer_id) return true;
        for (const auto& sid : m_selected_layer_ids)
            if (sid == id) return true;
        return false;
    };

    // Single VBO bind for all nav layers — eliminates N-1 redundant state changes.
    m_nav_merged_vbo.bind();
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    for (const auto& L : m_layers) {
        if (L.vertex_count <= 0 || !L.visible) continue;
        const bool hi = isHighlighted(L.id);
        QColor col = L.color.isValid() ? L.color : kNavLayerDefault;
        if (any_highlight && !hi) col.setAlphaF(col.alphaF() * 0.35f);
        m_shader->setUniformValue(m_loc_color,
            QVector4D(col.redF(), col.greenF(), col.blueF(), col.alphaF()));
        glLineWidth(hi ? 2.5f : (any_highlight ? 1.5f : 2.f));
        glDrawArrays(GL_LINES, L.vbo_start, L.vertex_count);
    }

    m_nav_merged_vbo.release();
}

void MapView3D::drawCurtains()
{
    const GLsizei stride = 4 * sizeof(float);
    for (auto& C : m_curtain_layers) {
        if (C.vertex_count <= 0 || !C.vbo.isCreated() || !C.visible) continue;
        C.vbo.bind();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        m_curtain_shader->setUniformValue(m_loc_curt_palette, C.palette_index);
        m_curtain_shader->setUniformValue(m_loc_curt_vexag,   m_v_exag);
        glDrawArrays(GL_TRIANGLES, 0, C.vertex_count);
        glDisableVertexAttribArray(1);
        C.vbo.release();
    }
}

void MapView3D::drawTerrain()
{
    m_terrain_shader->setUniformValue(m_loc_terr_campos, m_camera.position());

    // Switch to half-resolution LOD mesh when camera is more than 3.5× the
    // scene radius away — reduces triangle count ~4× with no perceptible change.
    const bool use_lod = m_camera.distance > m_scene_radius * 3.5f;

    for (auto& T : m_terrain_layers) {
        QOpenGLBuffer& vbo  = (use_lod && T.lod_vertex_count > 0) ? T.vbo_lod : T.vbo;
        const int      cnt  = (use_lod && T.lod_vertex_count > 0) ? T.lod_vertex_count
                                                                    : T.vertex_count;
        if (cnt <= 0 || !vbo.isCreated()) continue;

        vbo.bind();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);

        const float z_range = T.z_max - T.z_min;
        m_terrain_shader->setUniformValue(m_loc_terr_zmin,   T.z_min);
        m_terrain_shader->setUniformValue(m_loc_terr_zrange, z_range);
        m_terrain_shader->setUniformValue(m_loc_terr_vexag,  m_v_exag);

        glDrawArrays(GL_TRIANGLES, 0, cnt);
        vbo.release();
    }
}

void MapView3D::drawDrapeOutlines()
{
    if (m_active_layer_id.empty() && m_selected_layer_ids.empty()) return;

    auto isHighlighted = [&](const std::string& id) {
        if (id == m_active_layer_id) return true;
        for (const auto& sid : m_selected_layer_ids)
            if (sid == id) return true;
        return false;
    };

    for (auto& D : m_drape_layers) {
        if (!D.visible || !isHighlighted(D.id)) continue;
        if (D.outline_vert_count > 0 && D.outline_vbo.isCreated())
            drawLines(D.outline_vbo, D.outline_vert_count, kDrapeOutline, 2.f);
    }
}

void MapView3D::drawDrapes()
{
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.f, -2.f);

    for (auto& D : m_drape_layers) {
        if (!D.texture || !D.texture->isCreated() || !D.visible) continue;

        D.texture->bind(0);
        m_drape_shader->setUniformValue(m_loc_drape_tex, 0);
        m_drape_shader->setUniformValue(m_loc_drape_origin,
            QVector2D(D.bbox_x0, D.bbox_y0));
        m_drape_shader->setUniformValue(m_loc_drape_invext,
            QVector2D(D.bbox_w > 0.f ? 1.f / D.bbox_w : 0.f,
                      D.bbox_h > 0.f ? 1.f / D.bbox_h : 0.f));
        m_drape_shader->setUniformValue(m_loc_drape_vexag, m_v_exag);
        m_drape_shader->setUniformValue(m_loc_drape_alpha, 0.85f);

        if (!m_terrain_layers.empty()) {
            const bool use_lod = m_camera.distance > m_scene_radius * 3.5f;
            for (auto& T : m_terrain_layers) {
                QOpenGLBuffer& tvbo = (use_lod && T.lod_vertex_count > 0) ? T.vbo_lod : T.vbo;
                const int      tcnt = (use_lod && T.lod_vertex_count > 0) ? T.lod_vertex_count
                                                                            : T.vertex_count;
                if (tcnt <= 0 || !tvbo.isCreated()) continue;
                tvbo.bind();
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
                glEnableVertexAttribArray(0);
                glDrawArrays(GL_TRIANGLES, 0, tcnt);
                tvbo.release();
            }
        } else if (D.quad_vert_count > 0 && D.quad_vbo.isCreated()) {
            D.quad_vbo.bind();
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            glDrawArrays(GL_TRIANGLES, 0, D.quad_vert_count);
            D.quad_vbo.release();
        }

        D.texture->release();
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
}

} // namespace dolphin::ui
