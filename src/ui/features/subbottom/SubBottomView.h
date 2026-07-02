#pragma once
#include "core/SubBottomTrace.h"
#include "ui/features/subbottom/SubBottomViewStyle.h"
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <string>
#include <vector>

class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SubBottomView — 2-D seismic section widget.
//
//  X-axis: trace index (along-track), horizontally scrollable.
//  Y-axis: two-way travel time (depth), stretches to widget height.
//
//  Display controls (set from SubBottomDisplayPanel):
//    setPaletteIndex()   — SbpPalette::Index
//    setGain()           — amplitude multiplier before palette mapping
//    setShowBottomTrack() — draw pre-computed seabed pick overlay
//
//  Scale controls (set from SubBottomInspectorPanel or keyboard):
//    setPxPerTrace()     — horizontal pixels per trace column
//    setPxPerSample()    — vertical pixels per sample
//
//  Ctrl+scroll  → zoom trace width ±1 px
//  Shift+scroll → zoom depth scale ×1.25 / ÷1.25
//  Plain scroll → scroll along-track
// -----------------------------------------------------------------------------
class SubBottomView : public QWidget {
    Q_OBJECT
public:
    explicit SubBottomView(QWidget* parent = nullptr);

    void setTraces(std::vector<core::SubBottomTrace> traces);
    void clear();
    void scrollToTrace(int first_trace);

    int   traceCount()        const { return static_cast<int>(m_traces.size()); }
    int   firstVisibleTrace() const { return m_first_trace; }
    int   visibleTraceCount() const;

    // Scale
    void  setPxPerTrace (int px);
    void  setPxPerSample(float px);
    int   pxPerTrace()   const { return m_px_per_trace; }
    float pxPerSample()  const { return m_px_per_sample; }

    // Display — individual setters (each schedules a repaint)
    void setPaletteIndex   (int idx);
    void setGain           (float gain);
    void setContrast       (float contrast);
    void setPolarityInvert (bool invert);
    void setShowBottomTrack(bool show);
    // Batch setter — applies all five display params with a single repaint.
    void setDisplayParams  (int palette, float gain, float contrast,
                            bool polarity_invert, bool show_bottom_track);

    // Overlay style (crosshair, depth grid, BT colour/thickness)
    const SubBottomViewStyle& viewStyle() const { return m_style; }
    void setViewStyle(const SubBottomViewStyle& s);

    // -- Annotation tools --------------------------------------------------
    // Contact pick: 0=none, 1=pick. Feature draw: 0=none, 1=polygon, 2=line.
    // Activating one annotation tool deactivates the other.
    void setContactTool(int tool);
    void setFeatureTool(int tool);
    void setContactClass(const QString& cls) { m_contact_class = cls.toStdString(); }

signals:
    void scrollChanged(int first_trace, int total_traces, int visible_traces);
    void scaleChanged (int px_per_trace, float px_per_sample);
    void cursorMoved  (int trace_idx, float depth_s, double lat, double lon, bool nav_projected);
    void cursorLeft   ();
    void contextMenuRequested(const QPoint& global_pos);
    // Contact placed at a trace (geo = that trace's nav position).
    void contactPicked(int trace_idx, float depth_s, double lat, double lon,
                       bool is_projected);
    // Feature (polygon/polyline) drawn; vertices are (lon,lat) from trace nav.
    void featureDrawn(const std::vector<QPointF>& lonlat_vertices, bool polygon,
                      bool is_projected);

protected:
    void paintEvent        (QPaintEvent*        e) override;
    void resizeEvent       (QResizeEvent*       e) override;
    void mousePressEvent   (QMouseEvent*        e) override;
    void mouseMoveEvent    (QMouseEvent*        e) override;
    void mouseDoubleClickEvent(QMouseEvent*     e) override;
    void keyPressEvent     (QKeyEvent*          e) override;
    void wheelEvent        (QWheelEvent*        e) override;
    void leaveEvent        (QEvent*             e) override;
    void contextMenuEvent  (QContextMenuEvent*  e) override;

private:
    void   rebuildImage();
    QRgb   sampleToRgb(float sample) const;
    // Map a screen point to a trace index + travel-time depth and the trace's geo
    // position. Returns false when outside the loaded traces.
    bool   traceGeoAt(QPoint pos, int& trace_idx, float& depth_s,
                      double& lat, double& lon, bool& is_projected) const;
    void   paintAnnotationDraft(QPainter& p) const;

    std::vector<core::SubBottomTrace> m_traces;

    int   m_first_trace      = 0;
    int   m_px_per_trace     = 2;       // horizontal pixels per trace column
    float m_px_per_sample    = 0.5f;    // vertical pixels per sample

    // Display params
    int   m_palette_index     = 0;       // SbpPalette::Greyscale
    float m_gain              = 1.0f;
    float m_contrast          = 1.0f;
    bool  m_polarity_invert   = false;
    bool  m_show_bottom_track = true;

    SubBottomViewStyle m_style;

    int   m_cursor_x         = -1;
    int   m_cursor_y         = -1;

    // -- Annotation tool state ---------------------------------------------
    int                  m_contact_tool = 0;   // 0=none, 1=pick
    std::string          m_contact_class;       // applied to next contact pick
    int                  m_feature_tool = 0;    // 0=none, 1=polygon, 2=line
    std::vector<QPointF> m_feature_pts;         // committed vertices (lon,lat)
    std::vector<QPoint>  m_feature_px;          // matching screen points for the draft
    bool                 m_feature_proj = false;

    QImage m_image;
    bool   m_image_dirty     = true;
};

} // namespace dolphin::ui
