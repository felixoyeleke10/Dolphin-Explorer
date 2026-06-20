// MainWindow.Project.cpp — project CRUD is now in ProjectSessionController.
// This file retains only the two domain-mutation methods that still live in
// MainWindow because they reference m_undo_stack and UI state directly.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shell/AppInfo.h"
#include "app/project/Project.h"
#include "util/Json.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QUrl>

namespace {
constexpr const char* kRecentKey = "recentProjects";

// Read a project's stored display name from its .dlp manifest. Cached per path
// and invalidated by mtime so repeated menu/sidebar refreshes don't re-parse.
QString manifestStoredName(const QString& path)
{
    static QHash<QString, QPair<qint64, QString>> cache;   // path -> (mtime, name)
    const QFileInfo fi(path);
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    if (auto it = cache.constFind(path); it != cache.constEnd() && it->first == mtime)
        return it->second;

    QString name;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const auto json = dolphin::util::parseJson(f.readAll().toStdString());
        name = QString::fromStdString(json.get("name").asString());
    }
    if (name.isEmpty()) name = fi.baseName();   // fallback: file basename
    cache.insert(path, { mtime, name });
    return name;
}
} // namespace

namespace dolphin::ui {

QString MainWindow::recentDisplayName(const QString& path) const
{
    // The open project may have an unsaved rename — show its live name.
    if (auto* p = currentProject(); p && !p->manifestPath().empty()) {
        const QString open = QString::fromStdString(p->manifestPath());
        if (QFileInfo(open) == QFileInfo(path))
            return QString::fromStdString(p->name());
    }
    return manifestStoredName(path);
}

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
        const QString name = recentDisplayName(path);
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

void MainWindow::onRenameProject()
{
    auto project = currentProjectPtr();
    if (!project) return;

    bool ok = false;
    const QString current_name = QString::fromStdString(project->name());
    const QString name = QInputDialog::getText(
        this,
        tr("Rename Project"),
        tr("Project name:"),
        QLineEdit::Normal,
        current_name,
        &ok).trimmed();

    if (!ok || name.isEmpty() || name == current_name) return;

    // PSC owns the rename: display name + (for saved projects) the .dlp file move,
    // Recent list, window title, and dirty state. It emits recentProjectsChanged,
    // which already refreshes the menu + sidebar; we just refresh the layer tree.
    m_session_ctrl->renameProject(name);
    if (m_line_list) m_line_list->refresh();
    rebuildRecentMenu();
    QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
    refreshSidebarSections(s.value(kRecentKey).toStringList());
}

void MainWindow::onRemoveContact(uint64_t contact_id)
{
    if (!currentProject()) return;
    const auto& contacts = currentProject()->contacts();
    const auto it = std::find_if(contacts.begin(), contacts.end(),
        [contact_id](const core::Contact& c) { return c.id == contact_id; });
    if (it == contacts.end()) return;
    const QString label = QString::fromStdString(it->label);
    // Soft-delete to the recycle bin (undoable) — the single delete path.
    m_undo_stack->push(new RecycleContactCommand(currentProject(), contact_id));
    recordActivity(ActivityKind::ContactPick, tr("Contact %1 moved to Recycle Bin").arg(label));
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
