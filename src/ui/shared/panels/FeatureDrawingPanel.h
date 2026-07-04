#pragma once
#include <QWidget>

class QToolButton;

namespace dolphin::ui {

// FeatureDrawingPanel — the content of a "Feature Drawing" tool section.
//
// Simple row of drawing tools: Polygon, Line, Pen (freehand). Picking one activates
// that draw tool; picking it again (or another surface tool) turns it off. No type
// combo / classification — the geometry kind IS the tool.
//
// toolChanged emits: 0=none, 1=polygon, 2=line, 3=pen.
class FeatureDrawingPanel : public QWidget {
    Q_OBJECT
public:
    explicit FeatureDrawingPanel(QWidget* parent = nullptr);

    // Reflect the active tool on the buttons (signal-blocked; does not re-emit).
    void setActiveTool(int tool);

signals:
    void toolChanged(int tool);   // 0=none, 1=polygon, 2=line, 3=pen

private:
    QToolButton* m_poly = nullptr;
    QToolButton* m_line = nullptr;
    QToolButton* m_pen  = nullptr;
};

} // namespace dolphin::ui
