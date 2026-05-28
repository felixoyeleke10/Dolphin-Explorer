#pragma once
#include <QString>
#include <QScrollArea>
#include <string>
#include "pipeline/IProcessingNode.h"

class QLabel;
class QWidget;
class QVBoxLayout;

namespace dolphin::pipeline { class NodeGraph; }

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  NodeInspectorPanel — auto-generated parameter editor for a selected node.
//
//  Call showNode() when the canvas selection changes; clearNode() to reset.
//  Each parameter widget is built from NodeSchema::params, respecting type
//  (bool → QCheckBox, int → QSpinBox, float/double → QDoubleSpinBox,
//   string → QLineEdit) and min/max bounds.
// ─────────────────────────────────────────────────────────────────────────────
class NodeInspectorPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit NodeInspectorPanel(QWidget* parent = nullptr);

    void showNode(pipeline::NodeGraph* graph, const std::string& node_id);
    void showStatus(const QString& title, const QString& detail = {});
    void clearNode();

signals:
    void paramChanged();
    void importRequested();

private:
    void rebuild();

    pipeline::NodeGraph* m_graph   = nullptr;
    std::string          m_node_id;
    QString              m_status_title;
    QString              m_status_detail;
    QWidget*             m_content = nullptr;
};

} // namespace dolphin::ui
