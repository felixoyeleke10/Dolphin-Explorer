#pragma once
#include <QWidget>

class QToolButton;

namespace dolphin::ui {

class TvgEditorModeSwitch final : public QWidget {
    Q_OBJECT
public:
    enum Mode { Graph = 0, Numeric = 1 };
    explicit TvgEditorModeSwitch(QWidget* parent = nullptr);
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

signals:
    void modeChanged(dolphin::ui::TvgEditorModeSwitch::Mode mode);

private:
    QToolButton* m_graph = nullptr;
    QToolButton* m_numeric = nullptr;
    Mode m_mode = Graph;
};

} // namespace dolphin::ui
