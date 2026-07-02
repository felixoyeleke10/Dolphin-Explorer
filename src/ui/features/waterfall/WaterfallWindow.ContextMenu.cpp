// WaterfallWindow.ContextMenu.cpp — right-click context menu for the waterfall view.
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "app/layers/DataLayer.h"

#include <QMenu>

namespace dolphin::ui {

void WaterfallWindow::onContextMenu(QPoint globalPos)
{
    QMenu menu;
    menu.addAction(tr("Fit Width"), this, [this] {
        if (m_view) m_view->setHorizontalScale(0.f);
    });
    menu.addSeparator();
    menu.addAction(tr("Scroll to Top"), this, [this] {
        if (m_view) m_view->scrollToRow(0);
    });
    menu.addAction(tr("Scroll to End"), this, [this] {
        if (m_view) m_view->scrollToEnd();
    });
    menu.addSeparator();
    menu.addAction(tr("Previous Line"), this, [this] {
        emit prevLineRequested(m_layer ? m_layer->id : std::string{});
    })->setEnabled(m_has_prev_line);
    menu.addAction(tr("Next Line"), this, [this] {
        emit nextLineRequested(m_layer ? m_layer->id : std::string{});
    })->setEnabled(m_has_next_line);
    menu.addSeparator();
    menu.addAction(tr("Metadata…"), this, [this] {
        emit metadataRequested();
    });
    menu.addAction(tr("Settings…"), this, [this] {
        emit settingsRequested();
    });
    menu.exec(globalPos);
}

} // namespace dolphin::ui
