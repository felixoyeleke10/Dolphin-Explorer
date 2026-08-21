// BottomDockPanel.cpp — panel state: collapse, tab switch, persist/restore.
//
// Constructor / layout build  → BottomDockPanel.Layout.cpp
// Problems tab + slots        → BottomDockPanel.Problems.cpp
// Output / Jobs / Terminal    → BottomDockPanel.OutputJobs.cpp
#include "ui/bottom/BottomDockPanel.h"
#include "ui/shell/Theme.h"

#include <QIcon>
#include <QPixmap>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QTransform>

namespace dolphin::ui {

static void applyChevronState(QToolButton* btn, bool collapsed)
{
    if (!btn) return;
    QPixmap px = Theme::icon(":/icons/panel_chevron.svg").pixmap(14, 14);
    if (collapsed) {
        // Rotate 180° so chevron points up when panel is minimised
        btn->setIcon(QIcon(px.transformed(QTransform().rotate(180))));
        btn->setToolTip(QObject::tr("Restore panel"));
    } else {
        btn->setIcon(QIcon(px));
        btn->setToolTip(QObject::tr("Minimize panel"));
    }
}

void BottomDockPanel::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) return;
    m_collapsed = collapsed;

    if (collapsed) {
        m_handle->hide();
        m_stack->hide();
        setFixedHeight(kHeaderH);
    } else {
        m_handle->show();
        m_stack->show();
        setFixedHeight(kHandleH + kHeaderH + m_content_h);
    }
    applyChevronState(m_collapse_btn, collapsed);
    emit collapsedChanged(collapsed);
    saveState();
}

void BottomDockPanel::switchTab(int idx)
{
    m_active_tab = idx;
    m_stack->setCurrentIndex(idx);
    for (int i = 0; i < kTabCount; ++i)
        if (m_tab_btns[i]) m_tab_btns[i]->setChecked(i == idx);

    if (m_collapsed) setCollapsed(false);
    if (idx == 2) rebuildJobsTab();
}

void BottomDockPanel::saveState() const
{
    QSettings s;
    s.setValue("bottomPanel/collapsed",   m_collapsed);
    s.setValue("bottomPanel/contentH",    m_content_h);
    s.setValue("bottomPanel/activeTab",   m_active_tab);
}

void BottomDockPanel::restoreState()
{
    QSettings s;
    m_content_h  = s.value("bottomPanel/contentH",  kDefaultH).toInt();
    m_active_tab = s.value("bottomPanel/activeTab",  1).toInt();
    m_active_tab = std::clamp(m_active_tab, 0, kTabCount - 1);

    if (m_stack) m_stack->setCurrentIndex(m_active_tab);
    for (int i = 0; i < kTabCount; ++i)
        if (m_tab_btns[i]) m_tab_btns[i]->setChecked(i == m_active_tab);

    const bool collapsed = s.value("bottomPanel/collapsed", false).toBool();

    if (collapsed) {
        m_collapsed = true;
        if (m_handle) m_handle->hide();
        if (m_stack)  m_stack->hide();
        setFixedHeight(kHeaderH);
    } else {
        m_collapsed = false;
        setFixedHeight(kHandleH + kHeaderH + m_content_h);
    }
    applyChevronState(m_collapse_btn, m_collapsed);
}

} // namespace dolphin::ui
