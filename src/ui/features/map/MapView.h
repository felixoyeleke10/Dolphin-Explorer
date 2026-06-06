#pragma once
#include "ui/features/map/MapTypes.h"
#include "ui/shell/Theme.h"
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QWidget>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class QContextMenuEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QPixmap;
class QResizeEvent;
class QWheelEvent;

namespace dolphin::app { class Project; }

namespace dolphin::ui {

class MapView : public QWidget {
    Q_OBJECT
public:
    enum MapInputMode { ModePan = 0, ModeSelect, ModeZoom, ModeMeasure, ModePickContact };

    explicit MapView(QWidget* parent = nullptr);

    void setProject(app::Project* project);
    void setActiveLayer(const std::string& layer_id);
    void refreshLayerOrder();

    // Store pre-built layer map data (coverage ribbons + nav track) delivered
    // from a background thread.  Cheap: just stores the data and triggers a repaint.
    void setLayerMapData(const std::string& layer_id, LayerMapData data);

    // Replace only the preview image for an already-loaded layer.
    // Used by SidescanViewController for instant palette recoloring — no geometry
    // rebuild needed because coverage + nav track are palette-independent.
    void updatePreviewImage(const std::string& layer_id, QImage img);

    void removeLayerData    (const std::string& layer_id);
    void setLayerVisible    (const std::string& layer_id, bool visible);
    void clearAllLayerData  ();

    // Input mode: ModePan for pan+click-select, ModeSelect for rubber-band.
    void setInputMode(MapInputMode mode);

    // Multi-layer selection highlight (coverage polygon outline on map).
    void setSelectedLayers(const std::vector<std::string>& ids);

    // Per-layer nav track visibility (toggled via right-click in explorer).
    void setNavTrackVisible(const std::string& layer_id, bool visible);
    bool isNavTrackVisible (const std::string& layer_id) const;

    void fitToData();
    void fitToDataAndReset();  // fit + clear user-interaction flag so new layers auto-fit
    void fitToLayer(const std::string& layer_id);
    // Returns the layer_id of the topmost visible coverage layer under the pixel point.
    std::string hitTestLayer(QPoint px) const;
    void setSelectedContact(uint64_t id);
    void setShowGrid          (bool show);
    void setMapBgColor        (QColor c);
    void setGridColor         (QColor c);
    void setGratLabelSize     (int size);    // 0=Small/7pt, 1=Normal/8pt, 2=Large/10pt
    void setGratLabelRotated  (bool rotated);
    void setGratCoordFormat   (int fmt);     // 0=Auto, 1=Degrees, 2=E/N, 3=Both

    bool   isProjected()    const { return m_is_projected; }
    bool   userInteracted() const { return m_user_interacted; }
    bool   isLayerVisible (const std::string& id) const;
    QRectF layerPaintRect (const std::string& id) const;

    // Read back stored layer map data (used by MapViewportHost to forward to 3D view).
    const LayerMapData* layerData(const std::string& id) const;
    std::vector<std::string> layerDataIds() const;

signals:
    void cursorMoved(double lon, double lat);
    void contextMenuRequested(QPoint globalPos);
    // Emitted on single left-click hit (no Ctrl, ModePan only).
    void layerClicked(const std::string& layer_id);
    // Emitted for multi-select: Ctrl+click toggle or rubber-band release.
    // An empty vector means the selection was cleared.
    void layersSelected(const std::vector<std::string>& layer_ids);
    // Emitted in ModeMeasure: distance in metres from start point to cursor.
    // Emitted with -1.0 when the start point is reset (mode cleared).
    void measurementUpdated(double metres);
    // Emitted in ModePickContact when user clicks: lon/lat of the picked point.
    // MapView auto-resets to ModePan after emitting.
    void contactPickedOnMap(double lon, double lat);

    // Emitted from setLayerMapData after data is stored — allows MapViewportHost
    // to forward data to the 3D view without touching every call site.
    void layerDataUpdated(const std::string& layer_id);

protected:
    void paintEvent        (QPaintEvent*        event) override;
    void resizeEvent       (QResizeEvent*       event) override;
    void mousePressEvent      (QMouseEvent*        event) override;
    void mouseDoubleClickEvent(QMouseEvent*        event) override;
    void mouseReleaseEvent    (QMouseEvent*        event) override;
    void mouseMoveEvent       (QMouseEvent*        event) override;
    void wheelEvent        (QWheelEvent*        event) override;
    void leaveEvent        (QEvent*             event) override;
    void contextMenuEvent  (QContextMenuEvent*  event) override;

private:
    void    rebuildNavTrack();
    void    rebuildCombined();
    void    ensureCombined();   // rebuilds if m_combined_dirty, then clears the flag
    QPointF geoToPixel(double lon, double lat) const;
    QPointF pixelToGeo(QPointF px)             const;
    double  baseScale() const;

    // Paint phase helpers — MapViewPaint.*.cpp
    void paintEmptyState    (QPainter& p) const;
    void paintSonarLayers   (QPainter& p) const;
    void paintGraticule     (QPainter& p) const;
    void paintNavTrack      (QPainter& p) const;
    void paintProfileTracks (QPainter& p) const;  // colored scalar ribbon (Profile layers)
    void paintContacts      (QPainter& p) const;
    void paintMeasureOverlay(QPainter& p) const;
    void paintScaleAndBadges(QPainter& p) const;

    // Returns IDs of all visible layers whose coverage ribbons intersect px_rect.
    std::vector<std::string> layersInRect(QRect px_rect) const;
    // Zoom by factor centered on pos (pixel coords).
    void zoomAtPoint(QPointF pos, double factor);

    // -- View state ------------------------------------------------------------
    bool         m_show_grid           = true;
    QColor       m_map_bg_color        { Theme::kBg };
    QColor       m_grid_color          { Theme::kBorder };
    int          m_grat_label_size     = 1;     // 0=Small/7pt, 1=Normal/8pt, 2=Large/10pt
    bool         m_grat_label_rotated  = false;
    int          m_grat_coord_fmt      = 0;    // 0=Auto, 1=Degrees, 2=E/N, 3=Both
    QPointF      m_origin{0.0, 0.0};
    QPoint       m_drag_start;
    bool         m_dragging        = false;
    bool         m_drag_moved      = false;   // true once drag exceeds 4 px
    double       m_zoom            = 1.0;
    double       m_ref_lat         = 0.0;  // reference latitude for Mercator cos(lat) scaling
    bool         m_is_projected    = false;
    bool         m_needs_fit       = false;
    bool         m_user_interacted = false;  // suppresses auto-fit after user pans/zooms
    MapInputMode m_input_mode      = ModePan;
    bool         m_rubberbanding   = false;
    QPoint       m_rubberband_end;

    app::Project* m_project = nullptr;
    std::string   m_active_layer_id;

    // -- Per-layer data --------------------------------------------------------
    std::unordered_map<std::string, LayerMapData> m_layer_data;

    // -- Selection set (coverage polygon highlight) ----------------------------
    std::set<std::string> m_selected_layer_ids;

    // -- Combined nav track + bounding box ------------------------------------
    std::vector<QPointF> m_nav_track;
    double m_bbox_lon_min =  1e18, m_bbox_lon_max = -1e18;
    double m_bbox_lat_min =  1e18, m_bbox_lat_max = -1e18;
    bool   m_combined_dirty = false;  // true when m_layer_data changed since last rebuildCombined

    // -- Contact selection -----------------------------------------------------
    uint64_t m_selected_contact_id = 0;

    // -- Measure tool state (multi-point polyline) -----------------------------
    std::vector<QPointF> m_measure_pts_geo;              // confirmed anchor points (lon, lat)
    std::vector<QPoint>  m_measure_pts_px;               // confirmed anchor pixels
    std::vector<double>  m_measure_seg_dist;             // committed segment distances (metres)
    QPoint               m_measure_cursor_px;            // live cursor pixel
    double               m_measure_live_dist = 0.0;      // distance from last anchor to cursor

};

} // namespace dolphin::ui
