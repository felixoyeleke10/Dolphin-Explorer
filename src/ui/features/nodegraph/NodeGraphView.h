#pragma once
#include <QWidget>
#include <QPointF>
#include <QRectF>
#include <map>
#include <string>
#include <unordered_set>

class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace dolphin::pipeline { class NodeGraph; struct NodeGroup; }

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  NodeGraphView — interactive node graph canvas.
//
//  Pan    : Middle-drag  or  Alt+Left-drag
//  Zoom   : Scroll wheel (centred on cursor)
//  Select : Left-click node
//  Move   : Left-drag node
//  Connect: Drag from output port (right circle) → input port (left circle)
//  Delete : Delete/Backspace on selected node; right-click → Remove Connection
//  Frame  : Press F to fit all nodes
// ─────────────────────────────────────────────────────────────────────────────
class NodeGraphView : public QWidget {
    Q_OBJECT
public:
    explicit NodeGraphView(QWidget* parent = nullptr);

    void setGraph(pipeline::NodeGraph* graph);
    void frameAll();
    void autoLayout();
    void zoomBy(double factor, QPointF pivot_widget);
    void addNodeAt(const std::string& type_id, QPointF canvas_pos);
    void addNodeAtWidget(const std::string& type_id, QPointF widget_pos);
    void setPlacementPreview(const std::string& type_id, QPointF widget_pos);
    void clearPlacementPreview();

public slots:
    void addNode(const std::string& type_id);
    void deleteSelected();

signals:
    void nodeSelected(const std::string& node_id);  // empty = deselected
    void graphModified();

protected:
    void paintEvent         (QPaintEvent*)       override;
    void mousePressEvent    (QMouseEvent*)       override;
    void mouseMoveEvent     (QMouseEvent*)       override;
    void mouseReleaseEvent  (QMouseEvent*)       override;
    void wheelEvent         (QWheelEvent*)       override;
    void keyPressEvent      (QKeyEvent*)         override;
    void resizeEvent        (QResizeEvent*)      override;
    void contextMenuEvent   (QContextMenuEvent*) override;

private:
    QPointF canvasToWidget(QPointF canvas) const;
    QPointF widgetToCanvas(QPointF widget) const;

    QRectF  nodeRect  (const std::string& id) const;
    QPointF outPortPos(const std::string& id) const;
    QPointF inPortPos (const std::string& id, int port = 0) const;

    std::string hitNode   (QPointF wp) const;
    bool        hitOutPort(QPointF wp, std::string& id) const;
    bool        hitInPort (QPointF wp, std::string& id, int& port) const;
    bool        hitEdge   (QPointF wp, int& idx) const;

    void paintBackground      (QPainter& p);
    void paintGroups          (QPainter& p);
    void paintEdges           (QPainter& p);
    void paintEdgePreview     (QPainter& p);
    void paintPlacementPreview(QPainter& p);
    void paintNodes           (QPainter& p);
    void paintNode            (QPainter& p, const std::string& id, bool sel, bool hov);
    void paintRubberBand      (QPainter& p);
    void paintBezier          (QPainter& p, QPointF from, QPointF to,
                               bool selected, float alpha = 1.f);
    void paintPort            (QPainter& p, QPointF wc, bool is_output,
                               bool hovered, bool connected);

    QColor typeColor (const std::string& type_id) const;
    bool   isConnected(const std::string& id, bool output) const;
    QRectF groupBounds(const pipeline::NodeGroup& g) const;
    std::string hitGroup(QPointF cp) const;

    void computeAutoLayout();
    void groupSelected();
    void ungroupSelected();

    pipeline::NodeGraph* m_graph = nullptr;

    QPointF m_pan  {0.0, 0.0};
    double  m_zoom  = 1.0;

    // Selection — m_sel_node = primary (shown in inspector), m_sel_nodes = full set
    std::string                       m_sel_node;
    std::unordered_set<std::string>   m_sel_nodes;

    std::string m_hov_node;
    std::string m_hov_port_node;
    bool        m_hov_port_out = false;
    int         m_hov_edge     = -1;
    std::string m_hov_group;

    bool    m_node_drag    = false;
    QPointF m_drag_start_c;
    std::map<std::string, std::pair<float,float>> m_drag_origins;

    bool        m_edge_drag     = false;
    std::string m_edge_from;
    int         m_edge_to_port  = 0;  // which input port the drag is targeting
    QPointF     m_edge_cur_w;

    bool        m_place_preview = false;
    std::string m_place_type_id;
    QPointF     m_place_widget;

    bool    m_pan_drag   = false;
    QPoint  m_pan_start;
    QPointF m_pan_origin;

    // Rubber-band selection
    bool    m_rubber_active = false;
    QPointF m_rubber_start_w;
    QRectF  m_rubber_rect_w;

    // Group drag
    bool        m_group_drag  = false;
    std::string m_dragged_group;
    QPointF     m_group_drag_start_c;
    std::map<std::string, std::pair<float,float>> m_group_node_origins;

    static constexpr float kNodeW   =  96.f;
    static constexpr float kNodeH   =  36.f;
    static constexpr float kHdrH    =  36.f;
    static constexpr float kPortR   =   4.5f;
    static constexpr float kPortHit =  10.f;
    static constexpr float kRadius  =   4.f;
};

} // namespace dolphin::ui
