// MainWindow.Project.cpp — project CRUD is now in ProjectSessionController.
// This file retains only the two domain-mutation methods that still live in
// MainWindow because they reference m_undo_stack and UI state directly.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/AppInfo.h"
#include "app/project/Project.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QUrl>

namespace {
constexpr const char* kRecentKey = "recentProjects";
} // namespace

namespace dolphin::ui {

void MainWindow::rebuildRecentMenu()
{
    if (!m_recent_menu) return;
    m_recent_menu->clear();

    QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
    const QStringList list = s.value(kRecentKey).toStringList();

    if (list.isEmpty()) {
        m_recent_menu->addAction(tr("No recent projects"))->setEnabled(false);
        return;
    }

    for (const QString& path : list) {
        const QString name = QFileInfo(path).baseName();
        auto* act = m_recent_menu->addAction(name, this, [this, path]() {
            m_session_ctrl->openProjectPath(path.toStdString());
        });
        act->setToolTip(path);
    }
    m_recent_menu->addSeparator();
    m_recent_menu->addAction(tr("Clear Recent"), this, [this]() {
        QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
        s.remove(kRecentKey);
    });
}

void MainWindow::onRemoveContact(uint64_t contact_id)
{
    if (!currentProject()) return;
    const auto& contacts = currentProject()->contacts();
    const auto it = std::find_if(contacts.begin(), contacts.end(),
        [contact_id](const core::Contact& c) { return c.id == contact_id; });
    if (it == contacts.end()) return;
    const QString label = QString::fromStdString(it->label);
    m_undo_stack->push(new RemoveContactCommand(
        currentProject(), *it,
        [this]() { markProjectDirty(); }));
    recordActivity(ActivityKind::ContactPick, tr("Contact %1 removed").arg(label));
}

void MainWindow::onRevealSource(const std::string& source_id)
{
    if (!currentProject()) return;
    const auto* src = currentProject()->findSource(source_id);
    if (!src) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(QString::fromStdString(src->path)).dir().absolutePath()));
}

void MainWindow::onAutoSave()
{
    m_session_ctrl->autoSave();
}

} // namespace dolphin::ui
