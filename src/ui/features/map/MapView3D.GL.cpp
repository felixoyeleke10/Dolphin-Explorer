// MapView3D.GL.cpp — OpenGL lifecycle (initializeGL, resizeGL) and geometry builders.
//
// Shader sources are defined here because they are consumed only by initializeGL.

#include "ui/features/map/MapView3D.h"

#include <QOpenGLTexture>

#include <cmath>
#include <limits>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Flat-colour shader (axes / nav tracks / survey outline)
// -----------------------------------------------------------------------------

static const char* kVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

static const char* kFragSrc = R"glsl(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)glsl";

// -----------------------------------------------------------------------------
//  Terrain shader (depth-coloured triangle mesh)
// -----------------------------------------------------------------------------

// uVExag scales Z for display only; depth colour uses unscaled Z so the
// palette stays correct regardless of the current exaggeration setting.
static const char* kTerrVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4  uMVP;
uniform float uZMin;
uniform float uZRange;
uniform float uVExag;
out float vDepthT;
out vec3  vWorldPos;
void main() {
    vec3 pos = vec3(aPos.xy, aPos.z * uVExag);
    gl_Position = uMVP * vec4(pos, 1.0);
    vDepthT  = (uZRange > 0.001) ? clamp((aPos.z - uZMin) / uZRange, 0.0, 1.0) : 0.5;
    vWorldPos = pos;
}
)glsl";

// Cool→warm depth palette: t=0=deep (dark blue), t=1=shallow (sand).
// Lighting: screen-space partial derivatives reconstruct per-fragment normals
// (no extra VBO attributes needed).  Sun from NW at ~40° elevation.
static const char* kTerrFragSrc = R"glsl(
#version 330 core
in  float vDepthT;
in  vec3  vWorldPos;
uniform vec3  uCamPos;
out vec4  fragColor;

vec3 depthColor(float t) {
    if (t < 0.25) return mix(vec3(0.05, 0.07, 0.38), vec3(0.05, 0.42, 0.82), t / 0.25);
    if (t < 0.50) return mix(vec3(0.05, 0.42, 0.82), vec3(0.18, 0.72, 0.50), (t - 0.25) / 0.25);
    if (t < 0.75) return mix(vec3(0.18, 0.72, 0.50), vec3(0.90, 0.82, 0.20), (t - 0.50) / 0.25);
    return             mix(vec3(0.90, 0.82, 0.20), vec3(0.82, 0.66, 0.46), (t - 0.75) / 0.25);
}

void main() {
    // Reconstruct surface normal from screen-space partial derivatives
    vec3 dPdx = dFdx(vWorldPos);
    vec3 dPdy = dFdy(vWorldPos);
    vec3 N = normalize(cross(dPdx, dPdy));
    if (N.z < 0.0) N = -N;

    // Sun from NW at ~40° elevation
    vec3 L = normalize(vec3(-0.45, 0.45, 0.78));
    float ambient = 0.38;
    float diffuse = max(dot(N, L), 0.0) * 0.62;

    // Blinn-Phong specular for subtle wet-seabed sheen
    vec3 V    = normalize(uCamPos - vWorldPos);
    vec3 H    = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.10;

    vec3 base = depthColor(vDepthT);
    fragColor = vec4(base * (ambient + diffuse) + vec3(spec), 0.92);
}
)glsl";

// -----------------------------------------------------------------------------
//  Drape shader (sonar image projected onto terrain / flat quad)
//
//  UV is computed per-vertex from the world-space XY position relative to the
//  sonar image's bounding box.  Fragments outside the bbox or with alpha < 1%
//  (no sonar return) are discarded so the terrain depth-colour shows through.
// -----------------------------------------------------------------------------

static const char* kDrapeVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4  uMVP;
uniform vec2  uBBoxOrigin;   // SW corner in local metres
uniform vec2  uBBoxInvExt;   // component-wise 1.0 / (NE - SW)
uniform float uVExag;
out vec2 vUV;
void main() {
    vec3 pos = vec3(aPos.xy, aPos.z * uVExag);
    gl_Position = uMVP * vec4(pos, 1.0);
    vUV = (aPos.xy - uBBoxOrigin) * uBBoxInvExt;
}
)glsl";

static const char* kDrapeFragSrc = R"glsl(
#version 330 core
in  vec2      vUV;
uniform sampler2D uSonarTex;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    if (vUV.x < 0.0 || vUV.x > 1.0 || vUV.y < 0.0 || vUV.y > 1.0) discard;
    vec4 c = texture(uSonarTex, vUV);
    if (c.a < 0.01) discard;
    fragColor = vec4(c.rgb, c.a * uAlpha);
}
)glsl";

// -----------------------------------------------------------------------------
//  Curtain shader (SBP vertical profile ribbon)
//
//  Textured fence: each quad spans surface (z=0) to the full profile depth
//  along one segment of the nav track, sampling the layer's REAL profile
//  raster (curtain_image — actual trace samples, percentile-normalized).
//  Rows below a trace's data are transparent (discarded), so the bottom edge
//  follows the data. The SbpPalette is applied per-fragment from a uniform —
//  recolouring is free.  VE is a uniform so the user can change it without a
//  VBO rebuild.
// -----------------------------------------------------------------------------

static const char* kCurtainVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4  uMVP;
uniform float uVExag;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos.xy, aPos.z * uVExag, 1.0);
}
)glsl";

static const char* kCurtainFragSrc = R"glsl(
#version 330 core
in  vec2 vUV;
uniform sampler2D uTex;
uniform int       uPaletteIdx;
out vec4          fragColor;

// Mirrors SbpPalette::toRgb (0=Greyscale, 1=InvGrey, 2=Seismic-as-grey, 3=Thermal).
vec3 applyPalette(float a, int idx) {
    if (idx == 1) return vec3(1.0 - a);              // Inverted Grey
    if (idx == 3) {                                   // Thermal
        float r = clamp(a * 3.0,       0.0, 1.0);
        float g = clamp(a * 3.0 - 1.0, 0.0, 1.0);
        float b = clamp(1.0 - a * 4.0, 0.0, 1.0);
        return vec3(r, g, b);
    }
    return vec3(a);   // Greyscale (0) and Seismic (2 — magnitude only here)
}

void main() {
    vec4 s = texture(uTex, vUV);
    if (s.a < 0.5) discard;   // below this trace's data / gap column
    fragColor = vec4(applyPalette(s.r, uPaletteIdx), 0.92);
}
)glsl";

// -----------------------------------------------------------------------------
//  Procedural infinite ground-grid shader
//
//  A full-screen NDC quad is drawn.  The fragment shader unprojects each pixel
//  to a world-space ray, intersects it with the z=0 ground plane, then draws
//  anti-aliased minor + major grid lines via fwidth-based smoothstep —
//  automatically handling any zoom level without CPU rebuilds.
// -----------------------------------------------------------------------------

static const char* kGridVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;   // NDC xy: (-1,-1) → (1,1)
out vec2 vNDC;
void main() {
    vNDC = aPos;
    gl_Position = vec4(aPos, 0.9999, 1.0);
}
)glsl";

static const char* kGridFragSrc = R"glsl(
#version 330 core
in  vec2      vNDC;
uniform mat4  uInvVP;
uniform vec2  uCamXY;        // camera orbit target XY projected onto z=0
uniform float uMinorStep;    // minor grid spacing in metres
uniform float uMajorStep;    // major grid spacing (typically 10× minor)
uniform float uFadeNear;     // distance at which fade begins
uniform float uFadeFar;      // distance at which grid fully disappears
uniform vec3  uMinorCol;     // minor-line colour (set from CPU theme constants)
uniform vec3  uMajorCol;     // major-line colour
out vec4 fragColor;

float gridLine(vec2 pos, float spacing) {
    vec2 wrapped = fract(pos / spacing) - 0.5;
    vec2 fw = max(fwidth(pos / spacing), 1e-4);
    vec2 line = 1.0 - smoothstep(fw * 0.5, fw * 1.5, abs(wrapped));
    return max(line.x, line.y);
}

void main() {
    vec4 pn = uInvVP * vec4(vNDC, -1.0, 1.0);
    vec4 pf = uInvVP * vec4(vNDC,  1.0, 1.0);
    vec3 near = pn.xyz / pn.w;
    vec3 far  = pf.xyz / pf.w;
    vec3 dir  = far - near;

    if (abs(dir.z) < 1e-6) discard;
    float t = -near.z / dir.z;
    if (t < 0.0) discard;

    vec2 worldXY = near.xy + t * dir.xy;

    float dist = length(worldXY - uCamXY);
    float fade = 1.0 - smoothstep(uFadeNear, uFadeFar, dist);
    if (fade < 0.001) discard;

    float minor = gridLine(worldXY, uMinorStep);
    float major = gridLine(worldXY, uMajorStep);

    float alpha = max(minor * 0.15, major * 0.28);
    if (alpha < 0.005) discard;

    vec3 col = mix(uMinorCol, uMajorCol, major);
    fragColor = vec4(col, alpha * fade);
}
)glsl";

// -----------------------------------------------------------------------------
//  OpenGL lifecycle
// -----------------------------------------------------------------------------

void MapView3D::initializeGL()
{
    initializeOpenGLFunctions();
    m_fps_timer.start();

    glClearColor(0.055f, 0.055f, 0.063f, 1.f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Keep the widget single-sampled. MSAA makes Qt resolve an extra FBO during
    // top-level composition, which is a common source of flicker during Windows
    // screen capture on OpenGL-backed widgets.

    // -- Flat-colour shader ----------------------------------------------------
    m_shader = new QOpenGLShaderProgram(this);
    if (!m_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc) ||
        !m_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc) ||
        !m_shader->link()) {
        emit glInitError(tr("3D view: flat shader failed — %1").arg(m_shader->log()));
    }
    m_loc_mvp   = m_shader->uniformLocation("uMVP");
    m_loc_color = m_shader->uniformLocation("uColor");

    // -- Procedural grid shader ------------------------------------------------
    m_grid_shader = new QOpenGLShaderProgram(this);
    if (!m_grid_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,   kGridVertSrc) ||
        !m_grid_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, kGridFragSrc) ||
        !m_grid_shader->link()) {
        emit glInitError(tr("3D view: grid shader failed — %1").arg(m_grid_shader->log()));
    }
    m_loc_grid_invvp     = m_grid_shader->uniformLocation("uInvVP");
    m_loc_grid_camxy     = m_grid_shader->uniformLocation("uCamXY");
    m_loc_grid_minorstep = m_grid_shader->uniformLocation("uMinorStep");
    m_loc_grid_majorstep = m_grid_shader->uniformLocation("uMajorStep");
    m_loc_grid_fadenear  = m_grid_shader->uniformLocation("uFadeNear");
    m_loc_grid_fadefar   = m_grid_shader->uniformLocation("uFadeFar");
    m_loc_grid_minorcol  = m_grid_shader->uniformLocation("uMinorCol");
    m_loc_grid_majorcol  = m_grid_shader->uniformLocation("uMajorCol");

    // Static full-screen NDC quad (xy only, 2 triangles)
    static constexpr float kQuad[] = {
        -1.f, -1.f,   1.f, -1.f,   1.f,  1.f,
        -1.f, -1.f,   1.f,  1.f,  -1.f,  1.f,
    };
    m_grid_quad_vbo.create();
    m_grid_quad_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_grid_quad_vbo.bind();
    m_grid_quad_vbo.allocate(kQuad, sizeof(kQuad));
    m_grid_quad_vbo.release();

    // -- Terrain shader --------------------------------------------------------
    m_terrain_shader = new QOpenGLShaderProgram(this);
    if (!m_terrain_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,   kTerrVertSrc) ||
        !m_terrain_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, kTerrFragSrc) ||
        !m_terrain_shader->link()) {
        emit glInitError(tr("3D view: terrain shader failed — %1").arg(m_terrain_shader->log()));
    }
    m_loc_terr_mvp    = m_terrain_shader->uniformLocation("uMVP");
    m_loc_terr_zmin   = m_terrain_shader->uniformLocation("uZMin");
    m_loc_terr_zrange = m_terrain_shader->uniformLocation("uZRange");
    m_loc_terr_vexag  = m_terrain_shader->uniformLocation("uVExag");
    m_loc_terr_campos = m_terrain_shader->uniformLocation("uCamPos");

    // -- Curtain shader --------------------------------------------------------
    m_curtain_shader = new QOpenGLShaderProgram(this);
    if (!m_curtain_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,   kCurtainVertSrc) ||
        !m_curtain_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, kCurtainFragSrc) ||
        !m_curtain_shader->link()) {
        emit glInitError(tr("3D view: curtain shader failed — %1").arg(m_curtain_shader->log()));
    }
    m_loc_curt_mvp     = m_curtain_shader->uniformLocation("uMVP");
    m_loc_curt_palette = m_curtain_shader->uniformLocation("uPaletteIdx");
    m_loc_curt_vexag   = m_curtain_shader->uniformLocation("uVExag");
    m_loc_curt_tex     = m_curtain_shader->uniformLocation("uTex");

    // -- Drape shader ----------------------------------------------------------
    m_drape_shader = new QOpenGLShaderProgram(this);
    if (!m_drape_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex,   kDrapeVertSrc) ||
        !m_drape_shader->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, kDrapeFragSrc) ||
        !m_drape_shader->link()) {
        emit glInitError(tr("3D view: drape shader failed — %1").arg(m_drape_shader->log()));
    }
    m_loc_drape_mvp    = m_drape_shader->uniformLocation("uMVP");
    m_loc_drape_origin = m_drape_shader->uniformLocation("uBBoxOrigin");
    m_loc_drape_invext = m_drape_shader->uniformLocation("uBBoxInvExt");
    m_loc_drape_vexag  = m_drape_shader->uniformLocation("uVExag");
    m_loc_drape_tex    = m_drape_shader->uniformLocation("uSonarTex");
    m_loc_drape_alpha  = m_drape_shader->uniformLocation("uAlpha");

    m_vao.create();
    m_gl_ready = true;

    const QString renderer = QString::fromLatin1(
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    const QString version = QString::fromLatin1(
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    GLint max_tex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    emit gpuInfo(renderer, version, max_tex);
}

void MapView3D::resizeGL(int w, int h)
{
    m_camera.aspect = (h > 0) ? float(w) / float(h) : 1.f;
    glViewport(0, 0, w, h);
}

// -----------------------------------------------------------------------------
//  Geometry builders
// -----------------------------------------------------------------------------

void MapView3D::buildNavMergedVbo()
{
    // Concatenate all nav layer vertices into one VBO. Keeps a single bind per
    // frame in drawNavLayers instead of N binds — eliminates per-layer state change.
    std::vector<float> all_verts;
    all_verts.reserve(m_layers.size() * 2048);

    for (auto& L : m_layers) {
        L.vbo_start = static_cast<int>(all_verts.size()) / 3;

        QVector3D prev;
        bool have_prev = false;
        for (const auto& pt : L.raw_track) {
            if (std::isnan(pt.x()) || std::isnan(pt.y())) { have_prev = false; continue; }
            const QVector3D cur = toLocal(pt.x(), pt.y(), 0.0);
            if (have_prev) {
                all_verts.insert(all_verts.end(), {prev.x(), prev.y(), prev.z()});
                all_verts.insert(all_verts.end(), {cur.x(),  cur.y(),  cur.z()});
            }
            prev      = cur;
            have_prev = true;
        }
        L.vertex_count = static_cast<int>(all_verts.size()) / 3 - L.vbo_start;
        L.dirty        = false;
    }

    m_nav_merged_count = static_cast<int>(all_verts.size()) / 3;
    if (m_nav_merged_count > 0) {
        if (!m_nav_merged_vbo.isCreated()) {
            m_nav_merged_vbo.create();
            m_nav_merged_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        }
        m_nav_merged_vbo.bind();
        m_nav_merged_vbo.allocate(all_verts.data(),
                                   static_cast<int>(all_verts.size() * sizeof(float)));
        m_nav_merged_vbo.release();
    }
}

void MapView3D::buildTerrainVbo(TerrainMesh3D& tm)
{
    if (tm.cpu_verts.empty()) { tm.dirty = false; return; }

    // Full-resolution VBO
    tm.vertex_count = static_cast<int>(tm.cpu_verts.size()) / 3;
    if (!tm.vbo.isCreated()) {
        tm.vbo.create();
        tm.vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    tm.vbo.bind();
    tm.vbo.allocate(tm.cpu_verts.data(),
                    static_cast<int>(tm.cpu_verts.size() * sizeof(float)));
    tm.vbo.release();
    tm.cpu_verts.clear();
    tm.cpu_verts.shrink_to_fit();

    // Half-resolution LOD VBO
    if (!tm.lod_cpu_verts.empty()) {
        tm.lod_vertex_count = static_cast<int>(tm.lod_cpu_verts.size()) / 3;
        if (!tm.vbo_lod.isCreated()) {
            tm.vbo_lod.create();
            tm.vbo_lod.setUsagePattern(QOpenGLBuffer::StaticDraw);
        }
        tm.vbo_lod.bind();
        tm.vbo_lod.allocate(tm.lod_cpu_verts.data(),
                            static_cast<int>(tm.lod_cpu_verts.size() * sizeof(float)));
        tm.vbo_lod.release();
        tm.lod_cpu_verts.clear();
        tm.lod_cpu_verts.shrink_to_fit();
    }

    tm.dirty = false;
}

void MapView3D::rebuildAllTerrainVbos()
{
    for (auto& T : m_terrain_layers)
        if (T.dirty) buildTerrainVbo(T);
}

void MapView3D::uploadPendingDrapes()
{
    for (auto& D : m_drape_layers) {
        if (!D.dirty) continue;

        if (!D.pending_image.isNull()) {
            // Construct before destroying the old texture: if allocation throws,
            // D.texture remains valid and pointing at the previous resource.
            auto* tex = new QOpenGLTexture(D.pending_image);
            tex->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            tex->setMagnificationFilter(QOpenGLTexture::Linear);
            tex->setWrapMode(QOpenGLTexture::ClampToEdge);
            delete D.texture;
            D.texture = tex;
            buildDrapeQuad(D);
            D.pending_image = QImage();
        }

        if (!D.pending_hull.empty())
            buildDrapeHullVbo(D);

        D.dirty = false;
    }
}

void MapView3D::buildDrapeQuad(SonarDrape3D& D)
{
    const float x0 = D.bbox_x0,        y0 = D.bbox_y0;
    const float x1 = x0 + D.bbox_w,    y1 = y0 + D.bbox_h;

    const float verts[] = {
        x0, y0, 0.f,   x1, y0, 0.f,   x0, y1, 0.f,
        x1, y0, 0.f,   x1, y1, 0.f,   x0, y1, 0.f,
    };

    if (!D.quad_vbo.isCreated()) {
        D.quad_vbo.create();
        D.quad_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    D.quad_vbo.bind();
    D.quad_vbo.allocate(verts, sizeof(verts));
    D.quad_vbo.release();
    D.quad_vert_count = 6;
}

void MapView3D::buildDrapeHullVbo(SonarDrape3D& D)
{
    // pending_hull is a flat list of per-ribbon-pair polygon vertices separated by
    // NaN sentinels. Each segment is closed independently as a GL_LINES polygon.
    std::vector<float> verts;
    std::vector<QPointF> seg;

    auto flushPoly = [&]() {
        const int n = static_cast<int>(seg.size());
        if (n >= 3) {
            for (int i = 0; i < n; ++i) {
                const QVector3D a = toLocal(seg[static_cast<size_t>(i)].x(),
                                            seg[static_cast<size_t>(i)].y(), 0.0);
                const QVector3D b = toLocal(seg[static_cast<size_t>((i + 1) % n)].x(),
                                            seg[static_cast<size_t>((i + 1) % n)].y(), 0.0);
                verts.push_back(a.x()); verts.push_back(a.y()); verts.push_back(0.f);
                verts.push_back(b.x()); verts.push_back(b.y()); verts.push_back(0.f);
            }
        }
        seg.clear();
    };

    for (const auto& pt : D.pending_hull) {
        if (std::isnan(pt.x())) { flushPoly(); continue; }
        seg.push_back(pt);
    }
    flushPoly();

    D.pending_hull.clear();
    D.outline_vert_count = 0;
    if (verts.size() < 6) return;

    if (!D.outline_vbo.isCreated()) {
        D.outline_vbo.create();
        D.outline_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    D.outline_vbo.bind();
    D.outline_vbo.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));
    D.outline_vbo.release();
    D.outline_vert_count = static_cast<int>(verts.size()) / 3;
}

void MapView3D::buildCurtainVbo(CurtainLayer3D& C)
{
    if (C.cpu_verts.empty()) { C.dirty = false; return; }

    C.vertex_count = static_cast<int>(C.cpu_verts.size()) / 5;   // xyz + uv

    if (!C.vbo.isCreated()) {
        C.vbo.create();
        C.vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    C.vbo.bind();
    C.vbo.allocate(C.cpu_verts.data(),
                   static_cast<int>(C.cpu_verts.size() * sizeof(float)));
    C.vbo.release();

    // Profile raster → texture (same construct-before-destroy pattern as drapes).
    if (!C.pending_image.isNull()) {
        auto* tex = new QOpenGLTexture(C.pending_image);
        tex->setMinificationFilter(QOpenGLTexture::Linear);
        tex->setMagnificationFilter(QOpenGLTexture::Linear);
        tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        delete C.texture;
        C.texture       = tex;
        C.pending_image = QImage();
    }

    C.cpu_verts.clear();
    C.cpu_verts.shrink_to_fit();
    C.dirty = false;
}

void MapView3D::rebuildAllCurtainVbos()
{
    for (auto& C : m_curtain_layers)
        if (C.dirty) buildCurtainVbo(C);
}

void MapView3D::buildSurveyOutline()
{
    // Union of all nav-layer and terrain bboxes in local metres.
    float xmin =  std::numeric_limits<float>::max();
    float ymin =  std::numeric_limits<float>::max();
    float xmax = -std::numeric_limits<float>::max();
    float ymax = -std::numeric_limits<float>::max();
    bool any = false;

    for (const auto& L : m_layers) {
        if (!L.has_bbox) continue;
        xmin = std::min(xmin, L.bbox_xmin);  xmax = std::max(xmax, L.bbox_xmax);
        ymin = std::min(ymin, L.bbox_ymin);  ymax = std::max(ymax, L.bbox_ymax);
        any = true;
    }
    for (const auto& T : m_terrain_layers) {
        if (T.vertex_count <= 0 && T.cpu_verts.empty()) continue;
        xmin = std::min(xmin, T.bbox_xmin);  xmax = std::max(xmax, T.bbox_xmax);
        ymin = std::min(ymin, T.bbox_ymin);  ymax = std::max(ymax, T.bbox_ymax);
        any = true;
    }
    for (const auto& C : m_curtain_layers) {
        if (C.vertex_count <= 0 && C.cpu_verts.empty()) continue;
        xmin = std::min(xmin, C.bbox_xmin);  xmax = std::max(xmax, C.bbox_xmax);
        ymin = std::min(ymin, C.bbox_ymin);  ymax = std::max(ymax, C.bbox_ymax);
        any = true;
    }

    if (!any || xmin >= xmax || ymin >= ymax) { m_survey_count = 0; return; }

    const float verts[] = {
        xmin, ymin, 0.f,  xmax, ymin, 0.f,
        xmax, ymin, 0.f,  xmax, ymax, 0.f,
        xmax, ymax, 0.f,  xmin, ymax, 0.f,
        xmin, ymax, 0.f,  xmin, ymin, 0.f,
    };

    if (!m_survey_vbo.isCreated()) {
        m_survey_vbo.create();
        m_survey_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    }
    m_survey_vbo.bind();
    m_survey_vbo.allocate(verts, sizeof(verts));
    m_survey_vbo.release();
    m_survey_count = 8;
}

} // namespace dolphin::ui
