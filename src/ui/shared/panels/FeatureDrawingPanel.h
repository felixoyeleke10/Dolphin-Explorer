#pragma once
#include <QWidget>

class QComboBox;
class QToolButton;

namespace dolphin::ui {

// FeatureDrawingPanel — the content of a "Feature Drawing" tool section.
//
// A reusable section widget (used by the main-window right panel and the SBP
// viewer; the waterfall has its own inline section). Features are SHAPE
// annotations (polygon/polyline), distinct from point-pick contacts.
//
// toolChanged emits 0=off, 1=polygon, 2=line (draw toggle × type combo).
class FeatureDrawingPanel : public QWidget {
    Q_OBJECT
public:
    explicit FeatureDrawingPanel(QWidget* parent = nullptr);

    // Reflect the tool's active state back onto the toggle (signal-blocked).
    void    setDrawActive(bool active);
    QString classification() const;   // empty for "Unclassified"

signals:
    void toolChanged(int tool);       // 0=off, 1=polygon, 2=line
    void classificationChanged(const QString& classification);

private:
    void emitTool();

    QToolButton* m_draw_btn   = nullptr;
    QComboBox*   m_type_combo  = nullptr;  // 0=Polygon, 1=Line
    QComboBox*   m_class_combo = nullptr;
};

} // namespace dolphin::ui
