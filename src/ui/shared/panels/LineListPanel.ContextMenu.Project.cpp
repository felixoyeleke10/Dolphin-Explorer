// Project-root and source context menus for LineListPanel.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel_p.h"

#include "app/project/Project.h"

#include <QMenu>
#include <QTreeWidgetItem>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

void LineListPanel::showProjectContextMenu(const QPoint& global_pos)
{
    QMenu menu(this);

    menu.addAction(tr("Rename Project..."), this, [this] {
        emit renameProjectRequested();
    });
    menu.addSeparator();
    menu.addAction(tr("Save Project"), this, [this] {
        emit saveProjectRequested();
    });
    menu.addAction(tr("Save Project As..."), this, [this] {
        emit saveProjectAsRequested();
    });
    menu.addSeparator();
    auto* open_folder = menu.addAction(tr("Open Project Folder"), this, [this] {
        emit openProjectFolderRequested();
    });
    open_folder->setEnabled(m_project && !m_project->manifestPath().empty());
    menu.addSeparator();
    menu.addAction(tr("Close Project"), this, [this] {
        emit closeProjectRequested();
    });
    menu.addAction(tr("Delete Project..."), this, [this] {
        emit deleteProjectRequested();
    })->setEnabled(m_project && !m_project->manifestPath().empty());

    menu.exec(global_pos);
}

void LineListPanel::showSourceContextMenu(QTreeWidgetItem* item,
                                          const QPoint& global_pos)
{
    const std::string source_id = item->data(0, kRoleId).toString().toStdString();
    QMenu menu(this);
    menu.addAction(tr("Reveal in Explorer"), this, [this, source_id] {
        emit revealInExplorerRequested(source_id);
    });
    menu.exec(global_pos);
}

} // namespace dolphin::ui
