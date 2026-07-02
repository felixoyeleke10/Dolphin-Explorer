#pragma once
#include "ui/features/map/MapTypes.h"
#include "ui/systems/AppState.h"   // for ToolMode
#include "core/RasterGrid.h"
#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>
#include <string>
#include <unordered_map>
#include <vector>

class QLabel;
class QStackedWidget;
class QToolButton;
class QPushButton;

namespace dolphin::ui {

class MapView;
class MapView3D;

// -----------------------------------------------------------------------------
//  MapViewportHost — owns the 2D / 3D viewport stack.
//
//  Layout:
//    QStackedWidget  index 0 = MapView  (existing 2D raster map)
//                    index 1 = MapView3D (OpenGL 3D renderer)
//  An overlay toggle button switches between modes.
//
//  MainWindow keeps m_map_view = host->view2D() so all existing call sites
//  work unchanged.  Nav-track data is forwarded to the 3D view automatically
//  via the MapView::layerDataUpdated signal.
// -----------------------------------------------------------------------------

class MapViewportHost : public QWidget {
    Q_OBJECT
public:
    explicit MapViewportHost(QWidget* parent = nullptr);

    MapView*   view2D() const { return m_view2d; }
    MapView3D* view3D() const { return m_view3d; }

    bool isMode3D() const { return m_is_3d; }
    QImage grabViewportImage();

    // Forward layer data to the 3D view; called from the layerDataUpdated signal handler.
    void onLayerDataLoaded(const std::string& layer_id,
                       const LayerMapData& data,
                       QColor color = QColor(0, 180, 255));

    // Load a depth/bathy raster grid as a 3D terrain mesh (ensures the 3D view).
    void loadRasterTerrain(const std::string& layer_id, const core::RasterGrid& grid);

    // Call when a layer is removed.
    void onLayerRemoved(const std::string& layer_id);

    // Clear 3D scene (e.g. on project close).
    void clearScene();

public slots:
    void setMode3D      (bool on);
    void setShowGrid          (bool show);
    void setMapBgColor        (QColor c);
    void setGridColors        (QColor line2d, QColor minor3d, QColor major3d);
    void setGratLabelSize     (int size);
    void setGratLabelRotated  (bool rotated);
    void setGratCoordFormat   (int fmt);
    void setLayerVisible      (const std::string& layer_id, bool visible);
    void setNavTrackVisible   (const std::string& layer_id, bool visible);
    void setActiveLayer       (const std::string& layer_id);
    void setSelectedLayers (const std::vector<std::string>& ids);
    // Show or hide the "Import Files…" hint button overlaid on the empty map.
    void setShowImportHint (bool show);

    // Open a file dialog and load an XYZ/CSV terrain file into the 3D view.
    // No-op if not in 3D mode or scene origin is not set.
    void promptLoadTerrain();

    // Forward the active tool mode to the 3D view.  The 3D view uses the mode
    // to change its cursor and to decide what a left-click does (select vs.
    // place contact).  The 2D view handles its own InputMode via MapView directly.
    void setToolMode(ToolMode mode);

    // Programmatic viewport control — driven by status-bar spin boxes.
    void setViewportScale(double mpp);
    void setRotationDeg  (double deg);
    void panByPixels     (int dx, int dy);

signals:
    void modeChanged(bool is_3d);
    void gpuInfo(const QString& renderer, const QString& version, int maxTextureSize);
    void glInitError(const QString& message);
    // Cursor geo coordinates from whichever view is active.  Both NaN = left viewport.
    void cursorMoved(double lon, double lat);
    // Relayed from the active 2D view: ground metres per screen pixel + bearing.
    void viewportChanged(double metres_per_pixel, double rotation_deg);
    // Relayed from MapView3D — mirrors the equivalent signals on MapView.
    void layerClicked  (const std::string& layer_id);
    void layersSelected(const std::vector<std::string>& layer_ids);
    void contextMenuRequested(QPoint globalPos);
    // Relayed from MapView3D in ContactPick mode — same signature as MapView::contactPickedOnMap.
    void contactPickedAt(double lon, double lat);
    // Emitted when the user clicks the import hint button on the empty map.
    void importFilesRequested();

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    MapView3D* ensureView3D();
    void positionOverlay();

    MapView*        m_view2d              = nullptr;
    MapView3D*      m_view3d              = nullptr;   // native QOpenGLWindow
    QWidget*        m_view3d_container    = nullptr;   // createWindowContainer host (stack page)
    QStackedWidget* m_stack               = nullptr;
    QToolButton*    m_btn                 = nullptr;   // 2D / 3D toggle
    QToolButton*    m_terrain_btn         = nullptr;   // Load Terrain (3D mode only)
    QLabel*         m_terrain_label       = nullptr;   // "Loading…" feedback
    QWidget*        m_empty_state         = nullptr;   // transparent overlay: layout-centered empty-state CTA
    QPushButton*    m_import_hint_btn     = nullptr;   // button inside m_empty_state
    bool            m_is_3d               = false;
    QWidget*        m_transition_cover    = nullptr;

    // Cached viewport state — updated by every viewportChanged from either child.
    // Used to push the correct scale/rotation into the newly-active view on mode switch.
    double          m_last_mpp            = 0.0;
    double          m_last_rot_deg        = 0.0;

    // Cached display settings — applyLiveSettings always runs before ensureView3D(),
    // so these are populated before the 3D view is created and applied on creation.
    bool   m_grid_visible       = true;
    QColor m_map_bg_color;
    QColor m_grid_minor_3d;
    QColor m_grid_major_3d;
    int    m_grat_label_size    = 1;
    bool   m_grat_label_rotated = false;
    int    m_grat_coord_fmt     = 0;
    ToolMode m_tool_mode        = ToolMode::Pan;
    std::unordered_map<std::string, bool> m_layer_visibility;
};

} // namespace dolphin::ui
