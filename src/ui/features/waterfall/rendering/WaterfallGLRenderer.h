#pragma once
#include "ui/features/waterfall/rendering/WaterfallRenderer.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/waterfall/PingRow.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QRgb>
#include <vector>

namespace dolphin::ui {

// OpenGL 3.3 core-profile waterfall raster renderer.
//
// Textures uploaded once per data load:
//   u_port - 2D GL_R16,   width=max_ns_port, height=n_rows
//   u_stbd - 2D GL_R16,   width=max_ns_stbd, height=n_rows
//   u_lut  - 2D GL_RGBA8, width=256, height=1
//   u_src  - 2D GL_RGBA32F: port altitude, starboard altitude, max range
class WaterfallGLRenderer : protected QOpenGLFunctions_3_3_Core {
public:
    WaterfallGLRenderer() = default;
    ~WaterfallGLRenderer();

    bool initialize();
    void cleanup();

    void uploadAmplitude(const std::vector<PingRow>& rows);
    void uploadSrcParams(const std::vector<PingRow>& rows);
    void uploadLut(const QRgb* colour_table);

    void draw(const WfLayout& layout,
              int   scroll_row,
              float h_zoom,
              int   h_pan,
              bool  show_port,
              bool  show_stbd,
              bool  src_enabled,
              int   total_rows,
              const WaterfallParams& params);

    bool isReady() const { return m_ready; }
    bool hasAmplitude() const {
        return m_tex_n_rows > 0 && (m_tex_ns_port > 0 || m_tex_ns_stbd > 0);
    }

private:
    bool buildShaders();

    bool m_ready = false;

    QOpenGLShaderProgram* m_program = nullptr;
    GLuint m_vao      = 0;
    GLuint m_tex_port = 0;   // 2D R16
    GLuint m_tex_stbd = 0;   // 2D R16
    GLuint m_tex_lut  = 0;   // 2D RGBA8
    GLuint m_tex_src  = 0;   // 2D RGBA32F
    GLuint m_tex_port_range = 0; // 2D R32F authoritative slant ranges
    GLuint m_tex_stbd_range = 0;
    GLuint m_tex_counts = 0; // 2D RG32F actual port/starboard samples per row

    int m_tex_ns_port = 0;
    int m_tex_ns_stbd = 0;
    int m_tex_n_rows  = 0;

    // Tracks currently allocated GPU texture dimensions to allow glTexSubImage2D
    // re-upload (no re-allocation) when new data has the same dimensions.
    int m_alloc_ns_port = 0;
    int m_alloc_ns_stbd = 0;
    int m_alloc_n_rows  = 0;
    int m_alloc_src_n   = 0;
};

} // namespace dolphin::ui
