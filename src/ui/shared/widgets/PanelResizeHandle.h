#pragma once
#include "ui/shell/Theme.h"
#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>
#include <functional>

namespace dolphin::ui {

static constexpr int kHandleW = 4;  // drag handle width

// A 4 px wide drag handle placed on the left edge of a resizable panel.
// Calls on_resize() after each mouse move so the owner can reposition the panel.
class PanelResizeHandle : public QWidget {
    int*  m_width_ref;
    int   m_drag_start_x = 0;
    int   m_drag_start_w = 0;
    bool  m_dragging     = false;
    bool  m_hovered      = false;
    std::function<void()> m_on_resize;
public:
    PanelResizeHandle(int* width_ref, std::function<void()> on_resize, QWidget* parent)
        : QWidget(parent), m_width_ref(width_ref), m_on_resize(std::move(on_resize))
    {
        setFixedWidth(kHandleW);
        setCursor(Qt::SizeHorCursor);
        setAttribute(Qt::WA_Hover);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_hovered && !m_dragging) return;
        QColor c(Theme::kAccent);
        c.setAlphaF(m_dragging ? 0.55f : 0.35f);
        QPainter(this).fillRect(rect(), c);
    }
    void enterEvent(QEnterEvent*) override { m_hovered = true;  update(); }
    void leaveEvent(QEvent*)      override {
        if (!m_dragging) m_hovered = false;
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging     = true;
            m_drag_start_x = e->globalPosition().toPoint().x();
            m_drag_start_w = *m_width_ref;
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        int dx       = m_drag_start_x - e->globalPosition().toPoint().x();
        *m_width_ref = std::clamp(m_drag_start_w + dx, 180, 600);
        m_on_resize();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        m_dragging = false;
        update();
    }
};

} // namespace dolphin::ui
