#pragma once
#include "ui/features/map/Camera3D.h"
#include "ui/features/map/MapTypes.h"
#include "ui/shell/Theme.h"
#include "core/RasterGrid.h"

#include <QColor>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QElapsedTimer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWindow>
#include <QPoint>
#include <QString>
#include <QVector3D>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QOpenGLTexture;

namespace dolphin::ui {

struct TerrainBuildResult;   // defined in MapView3D.Load.cpp

// -----------------------------------------------------------------------------
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
// -----------------------------------------------------------------------------

// Native OpenGL surface (QOpenGLWindow, embedded via QWidget::createWindowContainer).
// Using a native window instead of a composited QOpenGLWidget keeps the rest of the
// app off the GL/DWM composition path, which is what eliminates the whole-window
// flicker on Windows (hover / alt-tab / popups). The trade-off — widgets can't float
// over a native surface — is why the viewport buttons live in a toolbar, not over it.
class MapView3D : public QOpenGLWindow, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit MapView3D(QWindow* parent = nullptr);
    ~MapView3D() override;

    // -- Scene origin ------------------------------------------------------
    void setSceneOrigin(double lon_or_x, double lat_or_y, bool is_projected);
    bool hasOrigin()  const { return m_has_origin; }
    bool isGLReady()  const { return m_gl_ready; }

    // -- Nav-track API -----------------------------------------------------
    void updateNavTrack(const std::string& layer_id,
                        const LayerMapData& data,
                        QColor color);
    void removeLayer(const std::string& layer_id);
    int navLayerCount() const { return static_cast<int>(m_layers.size()); }

    // -- Terrain API (Phase 2) ---------------------------------------------
    // Load an XYZ/CSV bathy file in a background thread; render as depth mesh.
    // z_is_depth: true → raw Z = positive depth → stored as negative altitude.
    // Does NOT require hasOrigin() — auto-detects origin from the data centre.
    void loadTerrainFile(const std::string& layer_id, const QString& path,
                         bool z_is_depth = true);
    // Build a terrain mesh directly from an in-memory depth/bathy raster grid
    // (imported GeoTIFF, etc.). Decimated to a manageable mesh resolution on a
    // background thread; auto-detects origin like loadTerrainFile.
    void loadTerrainGrid(const std::string& layer_id, const core::RasterGrid& grid,
                         bool z_is_depth = true);
    void removeTerrainLayer(const std::string& layer_id);
    int  terrainLayerCount() const { return static_cast<int>(m_terrain_layers.size()); }

    // -- Profile curtain API (Phase 2) ------------------------------------
    // Render the REAL SBP profile (data.curtain_image — actual trace samples,
    // percentile-normalized) as a textured vertical curtain along the nav
    // track. data.kind must be LayerMapKind::Profile. The palette is applied
    // in the shader from the current curtain palette (setCurtainPalette), so
    // recolouring never rebuilds geometry or textures.
    void setProfileCurtain(const std::string& layer_id, const LayerMapData& data);
    void removeProfileCurtain(const std::string& layer_id);
    int  curtainLayerCount() const { return static_cast<int>(m_curtain_layers.size()); }
    // SbpPalette::Index (0=Greyscale, 1=InvGrey, 2=Seismic, 3=Thermal) — keeps
    // the 3D curtains on the same palette as the SBP viewer / Views panel.
    void setCurtainPalette(int palette_index);

    // -- Sonar drape API (Phase 3) -----------------------------------------
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
                       const QImage& intensity_image,
                       const SonarDisplayParams& display_params,
                       double lon_min, double lat_min,
                       double lon_max, double lat_max,
                       std::vector<QPointF> hull_geo = {},
                       std::vector<QPointF> footprint_geo = {},
                       float opacity = 1.f);
    void removeSonarDrape(const std::string& layer_id);
    int  drapeLayerCount() const { return static_cast<int>(m_drape_layers.size()); }
    void setSonarPalette(int palette_index);
    int  sonarPalette() const { return m_sonar_palette; }

    // -- Scene -------------------------------------------------------------
    void clearScene();

    // -- Display -----------------------------------------------------------
    void setVerticalExaggeration(float ve);
    float verticalExaggeration() const { return m_v_exag; }
    void setShowGrid          (bool show);
    void setMapBgColor        (QColor c);
    void setGridColors        (QColor minor, QColor major);
    void setGratLabelSize     (int size);
    void setGratLabelRotated  (bool rotated);
    void setGratCoordFormat   (int fmt);
    void setLayerVisible      (const std::string& layer_id, bool visible);
    void setNavTrackVisible   (const std::string& layer_id, bool visible);
    void setLayerOpacity      (const std::string& layer_id, float opacity);  // drapes, [0,1]
    void setActiveLayer    (const std::string& layer_id);
    void setSelectedLayers (const std::vector<std::string>& ids);
    void setHoverTooltipsEnabled(bool on);
    void setHoverHighlightEnabled(bool on);

    // -- Camera ------------------------------------------------------------
    void resetCamera();
    void fitToScene();
    // Returns the layer under a screen point, or empty if no 3D layer was hit.
    std::string hitTestLayer(QPoint px) const;
    // Programmatic camera control — driven by status-bar spin boxes.
    void setYaw(double deg);
    void setDistance(float metres);

    // -- Tool mode ---------------------------------------------------------
    // Values intentionally match AppState::ToolMode: Pan=0, Select=1,
    // Zoom=2, Measure=3, ContactPick=4.
    void setToolMode(int mode);

signals:
    void terrainLoadFinished(const std::string& layer_id, bool success,
                             const QString& error_msg);
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
    // Emitted once, at the end of the first paintGL() call.
    // Used by MapViewportHost to hide the transition overlay.
    void firstFrameReady();
    // Emitted after every camera change (orbit, pan, zoom).
    // mpp = metres per pixel (approximation from camera distance + viewport height).
    // rotation_deg = camera yaw in degrees [0, 360).
    void viewportChanged(double metres_per_pixel, double rotation_deg);
    void hoverLayerChanged(const std::string& layer_id, QPoint global_pos);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent   (QMouseEvent*)        override;
    void mouseMoveEvent    (QMouseEvent*)        override;
    void mouseReleaseEvent (QMouseEvent*)        override;
    void wheelEvent        (QWheelEvent*)        override;
    bool event(QEvent*) override;
    // NOTE: QWindow has no leaveEvent/contextMenuEvent — the right-click context menu
    // is emitted from mouseReleaseEvent instead (see MapView3D.Input.cpp).

private:
    // -- Nav layer ---------------------------------------------------------
    struct NavLayer3D {
        std::string          id;
        QColor               color;
        std::vector<QPointF> raw_track;
        int                  vertex_count = 0;
        int                  vbo_start    = 0;   // vertex offset in m_nav_merged_vbo
        bool                 dirty         = true;
        bool                 layer_visible = true;
        bool                 nav_visible   = true;
        // SSS already communicates its position through the draped footprint.
        // Drawing the generated-colour centreline over it visually cuts the
        // port and starboard imagery into two separate strips.
        bool                 sonar_drape   = false;
        float bbox_xmin = 0.f, bbox_ymin = 0.f;
        float bbox_xmax = 0.f, bbox_ymax = 0.f;
        bool  has_bbox  = false;
    };

    // -- Terrain mesh layer ------------------------------------------------
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
        bool               visible = true;
    };

    // -- Profile curtain layer (SBP Phase 2) ------------------------------
    struct CurtainLayer3D {
        std::string        id;
        std::vector<float> cpu_verts;      // xyzUV quads (5 floats/vertex), GL_TRIANGLES
        QImage             pending_image;  // profile raster awaiting GL upload
        QOpenGLBuffer      vbo { QOpenGLBuffer::VertexBuffer };
        QOpenGLTexture*    texture      = nullptr;  // created in buildCurtainVbo (GL context)
        int                vertex_count = 0;
        float              z_range = 1.f;  // full profile depth in metres, for HUD legend
        float bbox_xmin = 0.f, bbox_ymin = 0.f;
        float bbox_xmax = 0.f, bbox_ymax = 0.f;
        float opacity = 1.f;   // user transparency [0,1] (Views ▸ SBP)
        bool  dirty   = true;
        bool  visible = true;
    };

    // -- Sonar drape layer -------------------------------------------------
    struct SonarDrape3D {
        std::string       id;
        // CPU side — set by setSonarDrape(), consumed by uploadPendingDrapes()
        QImage            pending_image;     // RGBA8888 flipped (row0=south)
        QImage            pending_fallback_image; // coloured fallback for raw upload failure
        std::vector<QPointF> pending_hull;   // geo coords of swath outline polygon
        std::vector<QPointF> pending_footprint; // independent fill/clip ribbons
        bool              dirty   = true;
        bool              visible = true;
        float             opacity = 1.f;     // user transparency [0,1] (Views ▸ SSS)
        // GPU side — created in uploadPendingDrapes() inside GL context
        QOpenGLTexture*   texture    = nullptr;
        bool              raw_intensity = false;
        quint64           source_key = 0;
        SonarDisplayParams display_params;
        // Sonar bbox in local metres (computed once from geo bbox + origin)
        float bbox_x0 = 0.f, bbox_y0 = 0.f;  // SW corner (lon_min, lat_min)
        float bbox_w  = 1.f, bbox_h  = 1.f;  // extent (lon_max-min, lat_max-min)
        // Fallback flat quad VBO (used when no terrain is loaded)
        QOpenGLBuffer     quad_vbo { QOpenGLBuffer::VertexBuffer };
        int               quad_vert_count = 0;
        // Swath hull outline VBO for selection rendering
        QOpenGLBuffer     outline_vbo { QOpenGLBuffer::VertexBuffer };
        int               outline_vert_count = 0;
        QOpenGLBuffer     footprint_vbo { QOpenGLBuffer::VertexBuffer };
        int               footprint_vert_count = 0;
    };

    // -- Geometry builders -------------------------------------------------
    void buildNavMergedVbo();   // rebuilds single VBO for all nav layers
    void buildCurtainVbo(CurtainLayer3D& C);
    void rebuildAllCurtainVbos();
    void buildTerrainVbo(TerrainMesh3D& tm);
    void rebuildAllTerrainVbos();
    uint64_t beginTerrainLoad(const std::string& layer_id);
    bool finishTerrainLoad(const std::string& layer_id, uint64_t generation);
    void invalidateTerrainLoad(const std::string& layer_id);
    // Apply a completed background terrain build (shared by file + grid loaders).
    void applyTerrainResult(const std::string& layer_id, TerrainBuildResult&& res);
    void uploadPendingDrapes();               // creates QOpenGLTexture + quad VBO in GL ctx
    void buildDrapeQuad(SonarDrape3D& d);    // flat z=0 quad covering drape bbox
    void buildDrapeHullVbo(SonarDrape3D& d); // swath polygon outline VBO from pending_hull
    void buildSurveyOutline();               // rectangle outline of overall data footprint

    // -- Draw phases -------------------------------------------------------
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
    void drawViewButtons(QPainter& painter);   // terrain-load feedback chip only

    void drawLines(QOpenGLBuffer& vbo, int count,
                   const QColor& color, float line_width = 1.f);
    void drawGridLabels(QPainter& painter);
    void updateCameraProjection();

    QVector3D toLocal(double x, double y, double z = 0.0) const;
    // Unproject pixel pos onto z=0 ground plane; returns false when the ray is
    // parallel to the plane or the origin is not yet set.  Fills geo with
    // (lon, lat) in the display CRS on success.
    bool groundHit(QPoint px, QPointF& geo) const;

    // -- Flat-colour shader (axes / nav tracks / survey outline) ----------
    QOpenGLShaderProgram* m_shader    = nullptr;
    GLint m_loc_mvp   = -1;
    GLint m_loc_color = -1;

    // -- Procedural grid shader (infinite ground plane, fragment-computed) -
    QOpenGLShaderProgram* m_grid_shader = nullptr;
    GLint m_loc_grid_invvp     = -1;
    GLint m_loc_grid_camxy     = -1;
    GLint m_loc_grid_minorstep = -1;
    GLint m_loc_grid_majorstep = -1;
    GLint m_loc_grid_fadenear  = -1;
    GLint m_loc_grid_fadefar   = -1;
    GLint m_loc_grid_minorcol  = -1;
    GLint m_loc_grid_majorcol  = -1;

    // -- Curtain shader (SBP depth ribbon — SbpPalette by amplitude) -----
    QOpenGLShaderProgram* m_curtain_shader = nullptr;
    GLint m_loc_curt_mvp     = -1;
    GLint m_loc_curt_palette = -1;
    GLint m_loc_curt_vexag   = -1;
    GLint m_loc_curt_tex     = -1;
    GLint m_loc_curt_alpha   = -1;
    int   m_curtain_palette  = 0;   // SbpPalette::Index shared by all curtains

    // -- Terrain shader (depth-coloured mesh + screen-space lighting) -----
    QOpenGLShaderProgram* m_terrain_shader = nullptr;
    GLint m_loc_terr_mvp    = -1;
    GLint m_loc_terr_zmin   = -1;
    GLint m_loc_terr_zrange = -1;
    GLint m_loc_terr_vexag  = -1;
    GLint m_loc_terr_campos = -1;

    // -- Drape shader (textured sonar image onto terrain/quad) -------------
    QOpenGLShaderProgram* m_drape_shader = nullptr;
    GLint m_loc_drape_mvp    = -1;
    GLint m_loc_drape_origin = -1;   // vec2: SW corner in local metres
    GLint m_loc_drape_invext = -1;   // vec2: 1/extent (for UV)
    GLint m_loc_drape_vexag  = -1;
    GLint m_loc_drape_tex    = -1;   // sampler2D: texture unit 0
    GLint m_loc_drape_alpha  = -1;
    GLint m_loc_drape_raw    = -1;
    GLint m_loc_drape_palette_tex = -1;
    GLint m_loc_drape_low = -1;
    GLint m_loc_drape_high = -1;
    GLint m_loc_drape_gain = -1;
    GLint m_loc_drape_contrast = -1;
    GLint m_loc_drape_threshold = -1;
    QOpenGLTexture* m_drape_palette_texture = nullptr;
    int m_sonar_palette = PaletteIndex::Greyscale;
    bool m_drape_palette_dirty = true;

    // -- Shared VAO + static VBOs ------------------------------------------
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_grid_quad_vbo  { QOpenGLBuffer::VertexBuffer }; // static NDC quad
    QOpenGLBuffer m_survey_vbo     { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_nav_merged_vbo { QOpenGLBuffer::VertexBuffer }; // all nav tracks batched
    int  m_nav_merged_count = 0;
    int  m_survey_count = 0;
    bool m_gl_ready            = false;
    bool m_first_frame_emitted = false;
    bool m_survey_dirty        = false;

    // -- Data layers -------------------------------------------------------
    std::vector<NavLayer3D>     m_layers;
    std::vector<CurtainLayer3D> m_curtain_layers;
    std::vector<TerrainMesh3D>  m_terrain_layers;
    std::vector<SonarDrape3D>   m_drape_layers;
    bool m_layers_dirty   = false;
    bool m_curtains_dirty = false;
    bool m_terrain_dirty  = false;
    bool m_drapes_dirty   = false;

    // -- Scene -------------------------------------------------------------
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

    bool  m_terrain_loading = false;   // drives the in-HUD "Loading terrain…" chip
    uint64_t m_next_terrain_load_generation = 0;
    std::unordered_map<std::string, uint64_t> m_terrain_load_generation;
    std::unordered_set<uint64_t> m_pending_terrain_loads;

    // -- Camera ------------------------------------------------------------
    Camera3D m_camera;

    // -- Tool mode ---------------------------------------------------------
    // 0=Pan, 1=Select, 2=Zoom, 3=Measure, 4=ContactPick (matches AppState::ToolMode).
    int m_tool_mode = 0;

    // -- Mouse -------------------------------------------------------------
    QPoint m_drag_start;
    float  m_drag_yaw0    = 0.f;
    float  m_drag_pitch0  = 0.f;
    bool   m_orbiting          = false;
    bool   m_orbit_moved       = false;  // true once right-drag exceeds click threshold
    bool   m_had_orbit         = false;  // survives mouseRelease; cleared on next press

    QPoint    m_pan_start;
    QVector3D m_pan_target0;
    bool      m_panning          = false;
    bool      m_pan_moved        = false;
    bool      m_camera_user_moved = false;  // suppresses auto-fit after user pans/orbits
    bool      m_hover_tooltips = false;
    bool      m_hover_highlight = false;
    std::string m_hover_layer_id;
    QPoint      m_hover_test_px { -9999, -9999 };

    // Screen-space layer pick; emits layerClicked / layersSelected.
    void pickAt(QPoint px);

    // -- Selection ---------------------------------------------------------
    std::vector<std::string> m_selected_layer_ids;
};

} // namespace dolphin::ui
