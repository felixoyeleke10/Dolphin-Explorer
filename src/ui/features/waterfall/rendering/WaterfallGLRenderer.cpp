// WaterfallGLRenderer.cpp - OpenGL 3.3 core-profile waterfall raster renderer.

#include "ui/features/waterfall/rendering/WaterfallGLRenderer.h"
#include "ui/features/waterfall/rendering/WaterfallRangeGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace dolphin::ui {

static const char* kVertSrc = R"glsl(
#version 330 core
const vec2 kPos[4] = vec2[4](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0));
void main() {
    gl_Position = vec4(kPos[gl_VertexID], 0.0, 1.0);
}
)glsl";

static const char* kFragSrc = R"glsl(
#version 330 core

uniform sampler2D u_port;
uniform sampler2D u_stbd;
uniform sampler2D u_src;
uniform sampler2D u_port_range;
uniform sampler2D u_stbd_range;

uniform float u_nadir_x;
uniform float u_z_port;
uniform float u_z_stbd;
uniform int   u_h_pan;
uniform int   u_scroll;
uniform int   u_n_rows;
uniform int   u_ns_port;
uniform int   u_ns_stbd;
uniform int   u_row_h;
uniform int   u_img_h;
uniform float u_scale_bar_h;
uniform float u_widget_h;
uniform bool  u_show_port;
uniform bool  u_show_stbd;
uniform bool  u_src_enabled;

uniform float u_display_low;
uniform float u_display_high;
uniform float u_gain;
uniform float u_contrast;
uniform float u_threshold;
uniform int   u_palette;

out vec4 fragColor;

float sampleForSlantRange(float target, bool is_port, int row, int ns) {
    int lo = 0;
    int hi = ns - 1;
    for (int iteration = 0; iteration < 20; ++iteration) {
        if (lo >= hi) break;
        int mid = (lo + hi) / 2;
        float value = is_port
            ? texelFetch(u_port_range, ivec2(mid, row), 0).r
            : texelFetch(u_stbd_range, ivec2(mid, row), 0).r;
        if (value < target) lo = mid + 1;
        else hi = mid;
    }
    int upper = clamp(lo, 0, ns - 1);
    int lower = max(0, upper - 1);
    float a = is_port
        ? texelFetch(u_port_range, ivec2(lower, row), 0).r
        : texelFetch(u_stbd_range, ivec2(lower, row), 0).r;
    float b = is_port
        ? texelFetch(u_port_range, ivec2(upper, row), 0).r
        : texelFetch(u_stbd_range, ivec2(upper, row), 0).r;
    float fraction = b > a ? clamp((target - a) / (b - a), 0.0, 1.0) : 0.0;
    return float(lower) + fraction;
}

const vec4 kBg    = vec4(0.0667, 0.0667, 0.0706, 1.0);
const vec4 kNadir = vec4(0.235,  0.235,  0.251,  1.0);

vec3 c(float r, float g, float b) {
    return vec3(r, g, b) / 255.0;
}

vec3 ramp(float t,
          float p0, vec3 c0, float p1, vec3 c1, float p2, vec3 c2,
          float p3, vec3 c3, float p4, vec3 c4, float p5, vec3 c5,
          float p6, vec3 c6, float p7, vec3 c7) {
    t = clamp(t, 0.0, 1.0);
    if (t <= p1) return mix(c0, c1, clamp((t - p0) / max(p1 - p0, 1e-6), 0.0, 1.0));
    if (t <= p2) return mix(c1, c2, clamp((t - p1) / max(p2 - p1, 1e-6), 0.0, 1.0));
    if (t <= p3) return mix(c2, c3, clamp((t - p2) / max(p3 - p2, 1e-6), 0.0, 1.0));
    if (t <= p4) return mix(c3, c4, clamp((t - p3) / max(p4 - p3, 1e-6), 0.0, 1.0));
    if (t <= p5) return mix(c4, c5, clamp((t - p4) / max(p5 - p4, 1e-6), 0.0, 1.0));
    if (t <= p6) return mix(c5, c6, clamp((t - p5) / max(p6 - p5, 1e-6), 0.0, 1.0));
    return mix(c6, c7, clamp((t - p6) / max(p7 - p6, 1e-6), 0.0, 1.0));
}

float displayTone(float v) {
    if (u_display_high > u_display_low + 1.0 / 65535.0)
        v = clamp((v - u_display_low) / (u_display_high - u_display_low), 0.0, 1.0);
    if (u_gain > 0.0)
        v = pow(v, 1.0 / u_gain);
    v = (v - 0.5) * u_contrast + 0.5;
    float thr = clamp(u_threshold, 0.0, 0.99);
    v = (v < thr) ? 0.0 : (v - thr) / (1.0 - thr);
    return clamp(v, 0.0, 1.0);
}

vec3 paletteColor(float t) {
    if (u_palette == 1) return vec3(t);
    if (u_palette == 2) return ramp(t,
        0.000, c(0,0,18), 0.170, c(0,22,68), 0.340, c(0,62,140), 0.510, c(0,115,195),
        0.670, c(0,175,218), 0.820, c(40,218,232), 0.940, c(155,240,248), 1.000, c(230,252,255));
    if (u_palette == 3) return ramp(t,
        0.000, c(6,2,0), 0.175, c(52,17,4), 0.360, c(120,52,14), 0.550, c(185,95,35),
        0.730, c(228,152,75), 0.880, c(248,210,148), 1.000, c(255,242,220), 1.000, c(255,242,220));
    if (u_palette == 4) return vec3(1.0 - t);
    if (u_palette == 5) return ramp(t,
        0.000, c(68,1,84), 0.143, c(71,44,122), 0.286, c(59,82,139), 0.429, c(44,113,142),
        0.571, c(33,145,140), 0.714, c(94,201,98), 0.857, c(172,220,52), 1.000, c(253,231,37));
    if (u_palette == 6) return ramp(t,
        0.000, c(13,8,135), 0.167, c(84,2,163), 0.333, c(139,10,165), 0.500, c(185,50,137),
        0.667, c(219,92,104), 0.833, c(244,136,73), 1.000, c(252,231,37), 1.000, c(252,231,37));
    if (u_palette == 7) return ramp(t,
        0.000, c(0,0,0), 0.200, c(10,3,60), 0.400, c(50,0,145), 0.600, c(140,0,210),
        0.800, c(240,70,220), 1.000, c(255,255,255), 1.000, c(255,255,255), 1.000, c(255,255,255));
    if (u_palette == 8) return ramp(t,
        0.000, c(15,10,5), 0.250, c(65,38,12), 0.500, c(140,95,45), 0.750, c(205,168,110),
        1.000, c(250,235,205), 1.000, c(250,235,205), 1.000, c(250,235,205), 1.000, c(250,235,205));
    if (u_palette == 9) return ramp(t,
        0.000, c(0,0,0), 0.125, c(0,0,210), 0.250, c(0,160,255), 0.375, c(0,230,200),
        0.500, c(0,210,0), 0.625, c(210,230,0), 0.750, c(255,120,0), 1.000, c(255,255,255));
    return ramp(t,
        0.000, c(5,0,0), 0.200, c(80,0,0), 0.380, c(200,0,0), 0.550, c(255,80,0),
        0.720, c(255,200,0), 0.870, c(255,255,100), 1.000, c(255,255,255), 1.000, c(255,255,255));
}

void main() {
    float sy    = u_widget_h - gl_FragCoord.y;
    float img_y = sy - u_scale_bar_h;
    if (img_y < 0.0)             { fragColor = kBg; return; }
    if (img_y >= float(u_img_h)) { fragColor = kBg; return; }

    float row_h = max(float(u_row_h), 1.0);
    float row_f = float(u_scroll) + (floor(img_y) + 0.5) / row_h - 0.5;
    if (row_f < -0.5 || row_f > float(u_n_rows) - 0.5) {
        fragColor = kBg;
        return;
    }

    float row_sample = clamp(row_f, 0.0, float(u_n_rows - 1));
    int   row        = int(floor(row_sample + 0.5));

    float sx      = gl_FragCoord.x;
    bool  is_port = sx < u_nadir_x;

    if ( is_port && !u_show_port) { fragColor = kBg; return; }
    if (!is_port && !u_show_stbd) { fragColor = kBg; return; }

    int   ns = is_port ? u_ns_port : u_ns_stbd;
    float z  = is_port ? u_z_port  : u_z_stbd;
    if (ns == 0 || z <= 0.0) { fragColor = kBg; return; }

    float si_f;
    if (u_src_enabled) {
        float src_tc = (float(row) + 0.5) / float(u_n_rows);
        vec4  sd     = texture(u_src, vec2(src_tc, 0.5));
        float alt    = is_port ? sd.r : sd.g;
        float max_r  = sd.b;

        if (alt > 0.0 && max_r > alt) {
            float port_w = float(u_ns_port > 0 ? u_ns_port : u_ns_stbd) * u_z_port;
            float stbd_w = float(u_ns_stbd > 0 ? u_ns_stbd : u_ns_port) * u_z_stbd;
            float frac   = is_port ? (u_nadir_x - 0.5 - sx) / port_w
                                   : (sx - u_nadir_x) / stbd_w;
            frac = clamp(frac, 0.0, 1.0);
            float max_g = sqrt(max_r * max_r - alt * alt);
            float g     = frac * max_g;
            float r     = sqrt(g * g + alt * alt);
            si_f = sampleForSlantRange(r, is_port, row, ns) + float(u_h_pan);
        } else {
            float dx = is_port ? (u_nadir_x - 0.5 - sx) : (sx - u_nadir_x);
            si_f = dx / z + float(u_h_pan);
        }
    } else {
        float dx = is_port ? (u_nadir_x - 0.5 - sx) : (sx - u_nadir_x);
        si_f = dx / z + float(u_h_pan);
    }

    si_f = clamp(si_f, 0.0, float(ns - 1));

    float row_tc  = (row_sample + 0.5) / float(u_n_rows);
    float samp_tc = (si_f + 0.5) / float(ns);
    float amp = is_port ? texture(u_port, vec2(samp_tc, row_tc)).r
                        : texture(u_stbd, vec2(samp_tc, row_tc)).r;

    fragColor = vec4(paletteColor(displayTone(amp)), 1.0);
}
)glsl";

WaterfallGLRenderer::~WaterfallGLRenderer() = default;

bool WaterfallGLRenderer::initialize()
{
    if (!initializeOpenGLFunctions()) return false;
    if (!buildShaders())              return false;
    glGenVertexArrays(1, &m_vao);
    m_ready = true;
    return true;
}

void WaterfallGLRenderer::cleanup()
{
    if (!m_ready) return;
    if (m_tex_port) { glDeleteTextures(1, &m_tex_port); m_tex_port = 0; }
    if (m_tex_stbd) { glDeleteTextures(1, &m_tex_stbd); m_tex_stbd = 0; }
    if (m_tex_src)  { glDeleteTextures(1, &m_tex_src);  m_tex_src  = 0; }
    if (m_tex_port_range) { glDeleteTextures(1, &m_tex_port_range); m_tex_port_range = 0; }
    if (m_tex_stbd_range) { glDeleteTextures(1, &m_tex_stbd_range); m_tex_stbd_range = 0; }
    if (m_vao)      { glDeleteVertexArrays(1, &m_vao);  m_vao      = 0; }
    delete m_program; m_program = nullptr;
    m_alloc_ns_port = 0;
    m_alloc_ns_stbd = 0;
    m_alloc_n_rows  = 0;
    m_alloc_src_n   = 0;
    m_ready = false;
}

bool WaterfallGLRenderer::buildShaders()
{
    m_program = new QOpenGLShaderProgram;
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc) ||
        !m_program->link()) {
        delete m_program;
        m_program = nullptr;
        return false;
    }
    return true;
}

void WaterfallGLRenderer::uploadAmplitude(const std::vector<PingRow>& rows)
{
    if (!m_ready || rows.empty()) return;

    const int n_rows = static_cast<int>(rows.size());
    int ns_port = 0, ns_stbd = 0;
    for (const auto& r : rows) {
        ns_port = std::max(ns_port, static_cast<int>(r.port.size()));
        ns_stbd = std::max(ns_stbd, static_cast<int>(r.stbd.size()));
    }

    auto uploadCh = [&](GLuint& tex, bool is_port, int ns) {
        const int upload_ns = std::max(1, ns);
        const size_t stride = static_cast<size_t>(upload_ns);
        std::vector<uint16_t> buf(stride * static_cast<size_t>(n_rows), 0);
        for (int ri = 0; ri < n_rows; ++ri) {
            const auto& src = is_port ? rows[ri].port : rows[ri].stbd;
            const int len = static_cast<int>(src.size());
            if (len > 0) {
                std::memcpy(buf.data() + static_cast<size_t>(ri) * stride,
                            src.data(),
                            static_cast<size_t>(std::min(len, upload_ns)) * sizeof(uint16_t));
            }
        }
        const bool is_new = !tex;
        if (is_new) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const int prev_ns = is_port ? m_alloc_ns_port : m_alloc_ns_stbd;
        if (!is_new && upload_ns == prev_ns && n_rows == m_alloc_n_rows) {
            // Same dimensions — update data in place, no GPU re-allocation.
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, upload_ns, n_rows,
                            GL_RED, GL_UNSIGNED_SHORT, buf.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, upload_ns, n_rows, 0,
                         GL_RED, GL_UNSIGNED_SHORT, buf.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (is_port) m_alloc_ns_port = upload_ns;
            else         m_alloc_ns_stbd = upload_ns;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    uploadCh(m_tex_port, true,  ns_port);
    uploadCh(m_tex_stbd, false, ns_stbd);

    auto uploadRanges = [&](GLuint& tex, bool is_port, int ns) {
        const int upload_ns = std::max(1, ns);
        const size_t stride = static_cast<size_t>(upload_ns);
        std::vector<float> buf(stride * static_cast<size_t>(n_rows), 0.f);
        for (int ri = 0; ri < n_rows; ++ri) {
            const auto& ranges = is_port ? rows[ri].port_ranges : rows[ri].stbd_ranges;
            const int actual_ns = static_cast<int>(
                is_port ? rows[ri].port.size() : rows[ri].stbd.size());
            const float max_r = std::isfinite(rows[ri].slant_range_m)
                ? rows[ri].slant_range_m : 0.f;
            for (int si = 0; si < actual_ns && si < upload_ns; ++si) {
                buf[static_cast<size_t>(ri) * stride + static_cast<size_t>(si)] =
                    waterfallRangeAtSample(ranges, actual_ns,
                                           static_cast<float>(si), max_r);
            }
            for (int si = actual_ns; si < upload_ns; ++si)
                buf[static_cast<size_t>(ri) * stride + static_cast<size_t>(si)] = max_r;
        }
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, upload_ns, n_rows, 0,
                     GL_RED, GL_FLOAT, buf.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    uploadRanges(m_tex_port_range, true, ns_port);
    uploadRanges(m_tex_stbd_range, false, ns_stbd);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_alloc_n_rows = n_rows;
    m_tex_ns_port = ns_port;
    m_tex_ns_stbd = ns_stbd;
    m_tex_n_rows  = n_rows;

    uploadSrcParams(rows);
}

void WaterfallGLRenderer::uploadSrcParams(const std::vector<PingRow>& rows)
{
    if (!m_ready) return;
    const int n = static_cast<int>(rows.size());
    if (n == 0) return;

    std::vector<float> data(static_cast<size_t>(n) * 4);
    for (int i = 0; i < n; ++i) {
        const size_t base = static_cast<size_t>(i) * 4;
        data[base] = waterfallSideAltitude(
            rows[i], core::SidescanChannel::Port);
        data[base + 1] = waterfallSideAltitude(
            rows[i], core::SidescanChannel::Starboard);
        data[base + 2] =
            std::isfinite(rows[i].slant_range_m) ? rows[i].slant_range_m : 0.f;
        data[base + 3] = 0.f;
    }
    const bool is_new_src = !m_tex_src;
    if (is_new_src) glGenTextures(1, &m_tex_src);
    glBindTexture(GL_TEXTURE_2D, m_tex_src);
    if (!is_new_src && n == m_alloc_src_n) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RGBA, GL_FLOAT, data.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, n, 1, 0, GL_RGBA, GL_FLOAT, data.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_alloc_src_n = n;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WaterfallGLRenderer::draw(const WfLayout& layout,
                                int scroll_row,
                                float h_zoom,
                                int h_pan,
                                bool show_port,
                                bool show_stbd,
                                bool src_enabled,
                                int /*total_rows*/,
                                const WaterfallParams& params)
{
    if (!m_ready || !m_program || !m_tex_port || !m_tex_stbd) return;
    if (m_tex_n_rows == 0) return;

    const qreal dpr = layout.dpr > 0.0 ? layout.dpr : 1.0;
    const int w      = std::max(1, layout.phys_w);
    const int h      = std::max(1, layout.phys_widget_h);
    const int nadir  = layout.phys_nadir;
    const int port_w = nadir - layout.phys_ruler_w;
    const int stbd_w = w - nadir;

    const float z_port = (h_zoom > 0.f) ? h_zoom * static_cast<float>(dpr)
                       : (port_w > 0 && m_tex_ns_port > 0
                          ? float(port_w) / m_tex_ns_port : 1.f);
    const float z_stbd = (h_zoom > 0.f) ? h_zoom * static_cast<float>(dpr)
                       : (stbd_w > 0 && m_tex_ns_stbd > 0
                          ? float(stbd_w) / m_tex_ns_stbd : 1.f);
    const int row_h = std::max(1, layout.phys_row_h);
    const float scale_bar_h = static_cast<float>(qRound(kWfScaleBarH * dpr));

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_tex_port);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_tex_stbd);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_tex_src ? m_tex_src : m_tex_port);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m_tex_port_range);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, m_tex_stbd_range);

    m_program->bind();
    m_program->setUniformValue("u_port",        0);
    m_program->setUniformValue("u_stbd",        1);
    m_program->setUniformValue("u_src",         2);
    m_program->setUniformValue("u_port_range",  3);
    m_program->setUniformValue("u_stbd_range",  4);
    m_program->setUniformValue("u_nadir_x",     float(nadir));
    m_program->setUniformValue("u_z_port",      z_port);
    m_program->setUniformValue("u_z_stbd",      z_stbd);
    m_program->setUniformValue("u_h_pan",       h_pan);
    m_program->setUniformValue("u_scroll",      scroll_row);
    m_program->setUniformValue("u_n_rows",      m_tex_n_rows);
    m_program->setUniformValue("u_ns_port",     m_tex_ns_port);
    m_program->setUniformValue("u_ns_stbd",     m_tex_ns_stbd);
    m_program->setUniformValue("u_row_h",       row_h);
    m_program->setUniformValue("u_img_h",       std::max(1, layout.phys_img_h));
    m_program->setUniformValue("u_scale_bar_h", scale_bar_h);
    m_program->setUniformValue("u_widget_h",    float(h));
    m_program->setUniformValue("u_show_port",   show_port);
    m_program->setUniformValue("u_show_stbd",   show_stbd);
    m_program->setUniformValue("u_src_enabled", src_enabled && m_tex_src != 0);
    m_program->setUniformValue("u_display_low", params.display_low);
    m_program->setUniformValue("u_display_high", params.display_high);
    m_program->setUniformValue("u_gain",        params.gain);
    m_program->setUniformValue("u_contrast",    params.contrast);
    m_program->setUniformValue("u_threshold",   params.threshold);
    m_program->setUniformValue("u_palette",     params.palette);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    m_program->release();

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

} // namespace dolphin::ui
