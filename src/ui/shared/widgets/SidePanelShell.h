#pragma once
#include <QFrame>

class QVBoxLayout;

namespace dolphin::ui {

// Shared container chrome for left/right dock panels (and the panes inside them).
//
// A SidePanelShell is a thin vertical stack: an optional fixed-height header or
// tab bar at the top, and a body that fills the remaining height. Both the File
// Explorer dock and each pane of the Properties panel use it so the two sides of
// the shell share one background/border contract and can never visually drift.
//
// Object-name contract (styled centrally in AppStylePanels.cpp): "sidePanelShell".
class SidePanelShell : public QFrame {
    Q_OBJECT
public:
    explicit SidePanelShell(QWidget* parent = nullptr);

    // Fixed strip at the top — a PanelTabBar or a custom title QFrame.
    // Replaces any previously-set header.
    void setHeader(QWidget* header);

    // Content area that fills the remaining height (stretch 1).
    // Replaces any previously-set body.
    void setBody(QWidget* body);

    QWidget* header() const { return m_header; }
    QWidget* body() const   { return m_body; }

private:
    QVBoxLayout* m_layout = nullptr;
    QWidget*     m_header = nullptr;
    QWidget*     m_body   = nullptr;
};

} // namespace dolphin::ui
