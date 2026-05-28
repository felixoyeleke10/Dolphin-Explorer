#pragma once
#include "ui/shell/Theme.h"
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QObject>
#include <QPushButton>
#include <QWidget>
#include <QWindow>

namespace dolphin::ui {

// Event filter installed on the QMenuBar to provide:
//   - True-centre positioning of a child search widget (adaptive to window width)
//   - Optional left flank (e.g. nav arrows) and right flank (e.g. action buttons)
//     positioned flush against the search widget with a 4 px gap each side
//   - Overflow "…" button when left-side menu items would be covered by search
//   - Frameless-window drag via click-and-drag on empty menu bar space
//   - Double-click to maximise / restore
class MenuBarFilter : public QObject {
    QMainWindow*    m_win;
    QWidget*        m_search       = nullptr;
    QWidget*        m_left_flank   = nullptr;  // positioned just left of search bar
    QWidget*        m_right_flank  = nullptr;  // positioned just right of search bar
    QPushButton*    m_overflow_btn = nullptr;
    QList<QAction*> m_hidden_actions;
    bool            m_in_update    = false;

public:
    explicit MenuBarFilter(QMainWindow* win) : QObject(win), m_win(win) {}

    void setSearchWidget(QWidget* w) { m_search = w; }
    void setLeftFlank(QWidget* w)    { m_left_flank  = w; }
    void setRightFlank(QWidget* w)   { m_right_flank = w; }

    void centreSearch(QMenuBar* mb)
    {
        if (!m_search || m_in_update) return;
        m_in_update = true;

        const auto actions = mb->actions();

        // Reset: show all actions so we can snapshot their natural geometry
        for (auto* a : actions) a->setVisible(true);

        // ── True-centre: centre in the full bar minus corner-widget width ─────
        const int bar_w    = mb->width();
        const int corner_w = [mb]() -> int {
            auto* cw = mb->cornerWidget(Qt::TopRightCorner);
            return cw ? cw->width() : 0;
        }();
        const int avail_w = bar_w - corner_w;

        const int sw = qBound(Theme::kCmdBarMinW,
                              bar_w * Theme::kCmdBarPct / 100,
                              Theme::kCmdBarMaxW);
        // True visual centre of the full window, clamped so the pill never
        // overlaps the corner widget on the right.
        const int x = qMax(0, qMin((bar_w - sw) / 2, avail_w - sw - 4));
        const int y = (mb->height() - m_search->height()) / 2;

        m_search->setFixedWidth(sw);
        m_search->move(x, y);

        // ── Snapshot all-visible geometries before touching visibility ────────
        // (actionGeometry recomputes on demand, so capture while all are shown)
        QHash<QAction*, int> right_edge;
        for (auto* a : actions)
            right_edge[a] = mb->actionGeometry(a).right();

        // ── Hide actions whose right edge intrudes into the clear zone ────────
        // The clear zone extends left to make room for the left-flank widget
        // (nav arrows) plus a gap, so menu items don't overlap them.
        const int left_flank_w = m_left_flank ? m_left_flank->width() : 0;
        const int search_left  = x - (left_flank_w > 0 ? left_flank_w + 10 : 6);
        m_hidden_actions.clear();
        for (auto* a : actions) {
            if (right_edge[a] > search_left) {
                a->setVisible(false);
                m_hidden_actions.append(a);
            }
        }

        // ── Overflow "…" button — lazily created, positioned after last item ──
        if (!m_overflow_btn) {
            m_overflow_btn = new QPushButton(QStringLiteral("…"), mb);
            m_overflow_btn->setObjectName("menuOverflowBtn");
            m_overflow_btn->setFlat(true);
            QObject::connect(m_overflow_btn, &QPushButton::clicked,
                             m_overflow_btn, [this, mb]() { showOverflow(mb); });
        }

        if (!m_hidden_actions.isEmpty()) {
            // Find the right edge of the last still-visible action using
            // the pre-hide snapshot (left items never shift when right items
            // are hidden, so the snapshot is still accurate).
            int last_right = 0;
            for (auto* a : actions) {
                if (a->isVisible())
                    last_right = qMax(last_right, right_edge[a]);
            }
            m_overflow_btn->setFixedHeight(mb->height());
            m_overflow_btn->adjustSize();
            m_overflow_btn->move(last_right + 2, 0);
            m_overflow_btn->show();
            m_overflow_btn->raise();
        } else {
            m_overflow_btn->hide();
        }

        // ── Left flank (back / forward nav arrows) ────────────────────────────
        if (m_left_flank) {
            const int lh = m_left_flank->height();
            m_left_flank->move(x - left_flank_w - 4, (mb->height() - lh) / 2);
            m_left_flank->show();
            m_left_flank->raise();
        }

        // ── Right flank (Conversation / Assistant action buttons) ────────────────
        if (m_right_flank) {
            const int rh = m_right_flank->height();
            m_right_flank->move(x + sw + 4, (mb->height() - rh) / 2);
            m_right_flank->show();
            m_right_flank->raise();
        }

        m_search->raise();
        m_in_update = false;
    }

    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        auto* mb = qobject_cast<QMenuBar*>(obj);
        if (!mb) return false;

        if (ev->type() == QEvent::Resize || ev->type() == QEvent::Show)
            centreSearch(mb);

        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton
                    && !mb->actionAt(me->pos())
                    && !mb->childAt(me->pos())) {
                if (auto* wh = m_win->windowHandle())
                    wh->startSystemMove();
                return true;
            }
        }
        if (ev->type() == QEvent::MouseButtonDblClick) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton
                    && !mb->actionAt(me->pos())
                    && !mb->childAt(me->pos())) {
                m_win->isMaximized() ? m_win->showNormal() : m_win->showMaximized();
                return true;
            }
        }
        return false;
    }

private:
    void showOverflow(QMenuBar* mb)
    {
        QMenu menu(mb);
        for (auto* act : std::as_const(m_hidden_actions)) {
            QString label = act->text();
            label.remove(QLatin1Char('&'));
            auto* proxy = menu.addAction(label);
            QObject::connect(proxy, &QAction::triggered, [mb, act]() {
                mb->setActiveAction(act);
            });
        }
        if (m_overflow_btn)
            menu.exec(m_overflow_btn->mapToGlobal(
                m_overflow_btn->rect().bottomLeft()));
    }
};

} // namespace dolphin::ui
