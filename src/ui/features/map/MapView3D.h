#pragma once
#include "ui/features/map/Camera3D.h"
#include "ui/features/map/MapTypes.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QElapsedTimer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector3D>

#include <string>
#include <vector>

class QOpenGLTexture;

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  MapView3D — true OpenGL 3D map renderer (Phase 1 + 2 + 3).
//
//  Renders in a Z-up, right-hand, local-metre coordinate system:
//    x = east  (metres from scene origin)
//    y = north (metres from scene origin)
//    z = altitude (metres; negative = depth)
//
//  Geometry layers:
//    • Flat seabed reference grid at z=0
//    • Nav-track polylines per layer
//    • Bathy terrain meshes (Phase 2): gridded XYZ/CSV files
//    • Sonar image drape (Phase 3): sidescan preview onto terrain or z=0 quad
//
//  Camera: orbit (left-drag = yaw/pitch, scroll = distance,
//                 right-drag = pan target).
// ─────────────────────────────────────────────────────────────────────────────

class MapView3D : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit MapView3D(QWidget* parent = nullptr);
    ~MapView3D() override;

    // ── Scene origin ──────────────────────────────────────────────────────
    void setSceneOrigin(double lon_or_x, double lat_or_y, bool is_projected);
    bool hasOrigin() const { return m_has_origin; }

    // ── Nav-track API ─────────────────────────────────────────────────────
    void updateNavTrack(const std::string& layer_id,
                        const LayerMapData& data,
                        QColor color);
    void removeLayer(const std::string& layer_id);

    // ── Terrain API (Phase 2) ─────────────────────────────────────────────
    // Load an XYZ/CSV bathy file in a background thread; render as depth mesh.
    // z_is_depth: true → raw Z = positive depth → stored as negative altitude.
    // Does NOT require hasOrigin() — auto-detects origin from the data centre.
    void loadTerrainFile(const std::string& layer_id, const QString& path,
                         bool z_is_depth = true);
    void removeTerrainLayer(const std::string& layer_id);
    int  terrainLayerCount() const { return static_cast<int>(m_terrain_layers.size()); }

    // ── Profile curtain API (Phase 2) ────────────────────────────────────
    // Render the SBP bottom-depth scalar as a colored vertical curtain along
    // the nav track.  data.kind must be LayerMapKind::Profile.
    // palette_index: SbpPalette::Index (0=Greyscale, 1=InvGrey, 2=Seismic, 3=Thermal).
    void setProfileCurtain(const std::string& layer_id, const LayerMapData& data,
                           int palette_index = 3);
    void removeProfileCurtain(const std::string& layer_id);
    int  curtainLayerCount() const { return static_cast<int>(m_curtain_layers.size()); }

    // ── Sonar drape API (Phase 3) ─────────────────────────────────────────
    // Drape a sidescan preview image onto all loaded terrain meshes (or onto a
    // flat reference quad at z=0 when no terrain is present).
    //
    // The image must cover exactly [lon_min, lon_max] × [lat_min, lat_max]
    // in the display CRS — which is exactly what LayerMapData::preview_image
    // guarantees.  Transparent pixels (no sonar return) are discarded in the
    // fragment shader so the terrain depth-colour shows through.
    //
    // Requires hasOrigin() == true; called automatically by MapViewportHost
    // whenever a swath with a preview image is loaded or its palette changes.
    void setSonarDrape(const std::string& layer_id,
                       const QImage& image,
                       double lon_min, double lat_min,
                       double lon_max, double lat_max,
                       std::vector<QPointF> hull_geo = {});
    void removeSonarDrape(const std::string& layer_id);
    int  drapeLayerCount() const { return static_cast<int>(m_drape_layers.size()); }

    // ── Scene ─────────────────────────────────────────────────────────────
    void clearScene();

    // ── Display ───────────────────────────────────────────────────────────
    void setVerticalExaggeration(float ve);
    float verticalExaggeration() const { return m_v_exag; }
    void setShowGrid          (bool show);
    void setMapBgColor        (QColor c);
    void setGridColors        (QColor minor, QColor major);
    void setGratLabelSize     (int size);
    void setGratLabelRotated  (bool rotated);
    void setGratCoordFormat   (int fmt);
    void setLayerVisible      (const std::string& layer_id, bool visible);
    void setActiveLayer    (const std::string& layer_id);
    void setSelectedLayers (const std::vector<std::string>& ids);

    // ── Camera ────────────────────────────────────────────────────────────
    void resetCamera();
    void fitToScene();
    // Returns the layer under a screen point, or empty if no 3D layer was hit.
    std::string hitTestLayer(QPoint px) const;

    // ── Tool mode ─────────────────────────────────────────────────────────
    // Values intentionally match AppState::ToolMode: Pan=0, Select=1,
    // Zoom=2, Measure=3, ContactPick=4.
    void setToolMode(int mode);

signals:
    void terrainLoadFinished(const std::string& layer_id, bool success,
                             const QString& error_msg);
    void loadTerrainRequested();
    // Emitted once after initializeGL with GPU renderer/version/max-texture-size.
    void gpuInfo(const QString& renderer, const QString& version, int max_tex_size);
    // Emitted for each shader that fails to compile or link during initializeGL.
    void glInitError(const QString& msg);
    // Emitted on every mouse-move with the geo coordinate under the cursor
    // (lon, lat in display CRS).  Both NaN when the cursor leaves the widget.
    void cursorMoved(double lon, double lat);
    // Selection signals — mirror MapView's interface so MainWindow can wire both views identically.
    void layerClicked  (const std::string& layer_id);
    void layersSelected(const std::vector<std::string>& layer_ids);
    void contextMenuRequested(QPoint globalPos);
    // Emitted in ContactPick mode when the user clicks without dragging.
    void contactPickedAt(double lon, double lat);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent   (QMouseEvent*)        override;
    void mouseMoveEvent    (QMouseEvent*)        override;
    void mouseReleaseEvent (QMouseEvent*)        override;
    void wheelEvent        (QWheelEvent*)        override;
    void leaveEvent        (QEvent*)             override;
    void contextMenuEvent  (QContextMenuEvent*)  override;

private:
    // ── Nav layer ─────────────────────────────────────────────────────────
    struct NavLayer3D {
        std::string          id;
        QColor               color;
        std::vector<QPointF> raw_track;
        int                  vertex_count = 0;
        int                  vbo_start    = 0;   // vertex offset in m_nav_merged_vbo
        bool                 dirty   = true;
        bool                 visible = true;
        float bbox_xmin = 0.f, bbox_ymin = 0.f;
        float bbox_xmax = 0.f, bbox_ymax = 0.f;
        bool  has_bbox  = false;
    };

    // ── Terrain mesh layer ────────────────────────────────────────────────
    struct TerrainMesh3D {
        std::string        id;
        std::vector<float> cpu_verts;       // full-res xyz triples, pending upload
        std::vector<float> lod_cpu_verts;   // half-res xyz triples, pending upload
        QOpenGLBuffer      vbo     { QOpenGLBuffer::VertexBuffer };
        QOpenGLBuffer      vbo_lod { QOpenGLBuffer::VertexBuffer };
        int                vertex_count     = 0;
        int                lod_vertex_count = 0;
        float              z_min = 0.f, z_max = 0.f;
        float              bbox_xmin = 0.f, bbox_ymin = 0.f;
        float              bbox_xmax = 0.f, bbox_ymax = 0.f;
        bool               dirty = false;
    };

    // ── Profile curtain layer (SBP Phase 2) ──────────────────────────────
    struct CurtainLayer3D {
        std::string        id;
        std::vector<float> cpu_verts;      // xyzA quads (4 floats/vertex), GL_TRIANGLES
        QOpenGLBuffer      vbo { QOpenGLBuffer::VertexBuffer };
        int                vertex_count = 0;
        float              z_range = 1.f;  // max depth in metres (> 0), for HUD legend
        int                palette_index = 3; // SbpPalette index (Thermal by default)
        float bbox_xmin = 0.f, bbox_ymin = 0.f;
        float bbox_xmax = 0.f, bbox_ymax = 0.f;
        bool  dirty   = true;
        bool  visible = true;
    };

    // ── Sonar drape layer ─────────────────────────────────────────────────
    struct SonarDrape3D {
        std::string       id;
        // CPU side — set by setSonarDrape(), consumed by uploadPendingDrapes()
        QImage            pending_image;     // RGBA8888 flipped (row0=south)
        std::vector<QPointF> pending_hull;   // geo coords of swath outline polygon
        bool              dirty   = true;
        bool              visible = true;
        // GPU side — created in uploadPendingDrapes() inside GL context
        QOpenGLTexture*   texture    = nullptr;
        // Sonar bbox in local metres (computed once from geo bbox + origin)
        float bbox_x0 = 0.f, bbox_y0 = 0.f;  // SW corner (lon_min, lat_min)
        float bbox_w  = 1.f, bbox_h  = 1.f;  // extent (lon_max-min, lat_max-min)
        // Fallback flat quad VBO (used when no terrain is loaded)
        QOpenGLBuffer     quad_vbo { QOpenGLBuffer::VertexBuffer };
        int               quad_vert_count = 0;
        // Swath hull outline VBO for selection rendering
        QOpenGLBuffer     outline_vbo { QOpenGLBuffer::VertexBuffer };
        int               outline_vert_count = 0;
    };

    // ── Geometry builders ─────────────────────────────────────────────────
    void buildNavMergedVbo();   // rebuilds single VBO for all nav layers
    void buildCurtainVbo(CurtainLayer3D& C);
    void rebuildAllCurtainVbos();
    void buildTerrainVbo(TerrainMesh3D& tm);
    void rebuildAllTerrainVbos();
    void uploadPendingDrapes();               // creates QOpenGLTexture + quad VBO in GL ctx
    void buildDrapeQuad(SonarDrape3D& d);    // flat z=0 quad covering drape bbox
    void buildDrapeHullVbo(SonarDrape3D& d); // swath polygon outline VBO from pending_hull
    void buildSurveyOutline();               // rectangle outline of overall data footprint

    // ── Draw phases ───────────────────────────────────────────────────────
    void drawGrid();
    void drawNavLayers();
    void drawCurtains();
    void drawTerrain();
    void drawSurveyOutline();
    void drawDrapes();
    void drawDrapeOutlines();
    void drawHUD(QPainter& painter);
    void drawCompassRose(QPainter& painter);
    void drawScaleBar3D(QPainter& painter);

    void drawLines(QOpenGLBuffer& vbo, int count,
                   const QColor& color, float line_width = 1.f);
    void drawGridLabels(QPainter& painter);

    QVector3D toLocal(double x, double y, double z = 0.0) const;
    // Unproject pixel pos onto z=0 ground plane; returns false when the ray is
    // parallel to the plane or the origin is not yet set.  Fills geo with
    // (lon, lat) in the display CRS on success.
    bool groundHit(QPoint px, QPointF& geo) const;

    // ── Flat-colour shader (axes / nav tracks / survey outline) ──────────
    QOpenGLShaderProgram* m_shader    = nullptr;
    GLint m_loc_mvp   = -1;
    GLint m_loc_color = -1;

    // ── Procedural grid shader (infinite ground plane, fragment-computed) ─
    QOpenGLShaderProgram* m_grid_shader = nullptr;
    GLint m_loc_grid_invvp     = -1;
    GLint m_loc_grid_camxy     = -1;
    GLint m_loc_grid_minorstep = -1;
    GLint m_loc_grid_majorstep = -1;
    GLint m_loc_grid_fadenear  = -1;
    GLint m_loc_grid_fadefar   = -1;
    GLint m_loc_grid_minorcol  = -1;
    GLint m_loc_grid_majorcol  = -1;

    // ── Curtain shader (SBP depth ribbon — SbpPalette by amplitude) ─────
    QOpenGLShaderProgram* m_curtain_shader = nullptr;
    GLint m_loc_curt_mvp     = -1;
    GLint m_loc_curt_palette = -1;
    GLint m_loc_curt_vexag   = -1;

    // ── Terrain shader (depth-coloured mesh + screen-space lighting) ─────
    QOpenGLShaderProgram* m_terrain_shader = nullptr;
    GLint m_loc_terr_mvp    = -1;
    GLint m_loc_terr_zmin   = -1;
    GLint m_loc_terr_zrange = -1;
    GLint m_loc_terr_vexag  = -1;
    GLint m_loc_terr_campos = -1;

    // ── Drape shader (textured sonar image onto terrain/quad) ─────────────
    QOpenGLShaderProgram* m_drape_shader = nullptr;
    GLint m_loc_drape_mvp    = -1;
    GLint m_loc_drape_origin = -1;   // vec2: SW corner in local metres
    GLint m_loc_drape_invext = -1;   // vec2: 1/extent (for UV)
    GLint m_loc_drape_vexag  = -1;
    GLint m_loc_drape_tex    = -1;   // sampler2D: texture unit 0
    GLint m_loc_drape_alpha  = -1;

    // ── Shared VAO + static VBOs ──────────────────────────────────────────
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_grid_quad_vbo  { QOpenGLBuffer::VertexBuffer }; // static NDC quad
    QOpenGLBuffer m_survey_vbo     { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_nav_merged_vbo { QOpenGLBuffer::VertexBuffer }; // all nav tracks batched
    int  m_nav_merged_count = 0;
    int  m_survey_count = 0;
    bool m_gl_ready     = false;
    bool m_survey_dirty = false;

    // ── Data layers ───────────────────────────────────────────────────────
    std::vector<NavLayer3D>     m_layers;
    std::vector<CurtainLayer3D> m_curtain_layers;
    std::vector<TerrainMesh3D>  m_terrain_layers;
    std::vector<SonarDrape3D>   m_drape_layers;
    bool m_layers_dirty   = false;
    bool m_curtains_dirty = false;
    bool m_terrain_dirty  = false;
    bool m_drapes_dirty   = false;

    // ── Scene ─────────────────────────────────────────────────────────────
    double      m_origin_x      = 0.0;
    double      m_origin_y      = 0.0;
    bool        m_has_origin    = false;
    bool        m_is_projected  = false;
    float       m_v_exag           = 1.f;
    float       m_scene_radius     = 500.f;
    bool        m_show_grid           = true;
    bool        m_show_fps            = false;
    int         m_grat_label_size     = 1;     // 0=Small/7pt, 1=Normal/8pt, 2=Large/10pt
    bool        m_grat_label_rotated  = false;
    int         m_grat_coord_fmt      = 0;    // 0=Auto, 1=Degrees, 2=E/N, 3=Both
    QColor      m_map_bg_color     { Theme::kBg };
    QColor      m_grid_minor_color { Theme::kGrid3DMinor };
    QColor      m_grid_major_color { Theme::kGrid3DMajor };
    std::string m_active_layer_id;

    // FPS tracking — rolling average over kFpsWindow frames
    QElapsedTimer m_fps_timer;
    float         m_fps_avg     = 0.f;
    int           m_fps_frames  = 0;

    // ── Camera ────────────────────────────────────────────────────────────
    Camera3D m_camera;

    // ── Tool mode ─────────────────────────────────────────────────────────
    // 0=Pan, 1=Select, 2=Zoom, 3=Measure, 4=ContactPick (matches AppState::ToolMode).
    int m_tool_mode = 0;

    // ── Mouse ─────────────────────────────────────────────────────────────
    QPoint m_drag_start;
    float  m_drag_yaw0    = 0.f;
    float  m_drag_pitch0  = 0.f;
    bool   m_orbiting     = false;
    bool   m_orbit_moved  = false;  // true once left-drag exceeds the click threshold

    QPoint    m_pan_start;
    QVector3D m_pan_target0;
    bool      m_panning   = false;
    bool      m_pan_moved = false;

    // Screen-space layer pick; emits layerClicked / layersSelected.
    void pickAt(QPoint px);

    // ── Selection ─────────────────────────────────────────────────────────
    std::vector<std::string> m_selected_layer_ids;
};

} // namespace dolphin::ui
