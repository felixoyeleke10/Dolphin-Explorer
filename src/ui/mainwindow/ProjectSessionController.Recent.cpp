// ProjectSessionController.Recent.cpp — recent-project persistence and title presentation.
#include "ui/mainwindow/ProjectSessionController.h"
#include "ui/shell/AppInfo.h"
#include "app/project/Project.h"
#include <QSettings>

namespace {
constexpr int kMaxRecent = 8;
constexpr const char* kRecentKey = "recentProjects";
}

namespace dolphin::ui {

QStringList ProjectSessionController::recentProjects() const
{
    QSettings settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    return settings.value(kRecentKey).toStringList();
}

void ProjectSessionController::addToRecentProjects(const QString& path)
{
    if (path.isEmpty()) return;
    QSettings settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    QStringList paths = settings.value(kRecentKey).toStringList();
    paths.removeAll(path);
    paths.prepend(path);
    if (paths.size() > kMaxRecent) paths.resize(kMaxRecent);
    settings.setValue(kRecentKey, paths);
    emit recentProjectsChanged(paths);
}

void ProjectSessionController::emitWindowTitle()
{
    emit windowTitleChanged(buildWindowTitle());
}

QString ProjectSessionController::buildWindowTitle() const
{
    if (!m_project) return tr("Dolphin Explorer");
    QString title;
    if (m_project_dirty) title += QStringLiteral("• ");
    title += QString::fromStdString(m_project->name());
    if (m_project->isTempProject()) title += tr(" (unsaved)");
    title += QStringLiteral(" — Dolphin Explorer");
    return title;
}

} // namespace dolphin::ui
