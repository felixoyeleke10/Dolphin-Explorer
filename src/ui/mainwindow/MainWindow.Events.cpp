// MainWindow.Events.cpp — OS-level event overrides: close, show, drag/drop, native WM.
#include "ui/mainwindow/MainWindow.h"
#include "app/project/Project.h"
#include "ui/features/import/ImportController.h"
#include "ui/shared/widgets/LayerPickerWidget.h"

#include "ui/shell/AppInfo.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <windowsx.h>
#endif

namespace dolphin::ui {

// Remove abandoned Session_* project folders from a previous run that were
// never promoted to a named project (temp projects the user discarded).
// Runs once on the first show event; older-than-kMaxAgeDays folders are deleted.
static void purgeStaleTempProjects()
{
    constexpr int kMaxAgeDays = 7;
    const QString projects_root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/projects";

    QDir root(projects_root);
    if (!root.exists()) return;

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kMaxAgeDays);
    const QStringList entries = root.entryList({"Session_*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : entries) {
        const QString full = projects_root + "/" + name;
        QDirIterator it(full, {"*.dlp"}, QDir::Files);
        bool has_named_project = false;
        while (it.hasNext()) {
            it.next();
            if (!it.fileName().startsWith("Session_")) { has_named_project = true; break; }
        }
        if (has_named_project) continue;

        const QFileInfo fi(full);
        if (fi.lastModified() < cutoff)
            QDir(full).removeRecursively();
    }
}

// -- Event overrides -----------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Imports still parsing? Closing used to abandon them silently, leaving
    // dead placeholder layers in the saved project ("imported three lines,
    // only one works"). Warn first; abandoned lines self-heal on next open.
    if (m_import_ctrl && m_import_ctrl->importsBusy()) {
        const auto reply = QMessageBox::warning(
            this, tr("Imports in progress"),
            tr("File imports are still running. Closing now abandons them —\n"
               "unfinished lines will be re-imported the next time this\n"
               "project is opened.\n\nClose anyway?"),
            QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) { event->ignore(); return; }
    }

    auto* proj = currentProject();
    if (proj && isProjectDirty()) {
        const auto reply = QMessageBox::question(
            this, tr("Close"),
            proj->isTempProject()
                ? tr("You have unsaved changes. Save before closing?")
                : tr("Save changes to \"%1\" before closing?")
                      .arg(QString::fromStdString(proj->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) { event->ignore(); return; }
        if (reply == QMessageBox::Save) {
            onSaveProject();
            if (isProjectDirty()) { event->ignore(); return; }  // save failed or was cancelled
        }
    } else if (proj && !proj->isTempProject()) {
        proj->save();
    }

    QSettings settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    settings.setValue("geometry",          saveGeometry());
    settings.setValue("ui/propsOpen",      m_props_open);
    settings.setValue("ui/propsWidth",     m_props_width);
    settings.setValue("ui/toolbarVisible", rightToolBarVisible());
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (auto* cw = centralWidget(); cw && m_layer_picker)
        m_layer_picker->reposition(cw->size(), 0);

    static bool s_purged = false;
    if (!s_purged) {
        s_purged = true;
        QTimer::singleShot(0, [] { purgeStaleTempProjects(); });
    }

}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const auto& url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (!paths.isEmpty())
        showImportDialog(paths);
}

bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (event_type == "windows_generic_MSG") {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            const LONG sx = GET_X_LPARAM(msg->lParam);
            const LONG sy = GET_Y_LPARAM(msg->lParam);
            const LONG y  = sy - frameGeometry().top();
            const LONG x  = sx - frameGeometry().left();
            if (y >= 0 && y < menuBar()->height() && x < width() - 140) {
                const QPoint mb_pos = menuBar()->mapFromGlobal(QPoint(sx, sy));
                if (!menuBar()->actionAt(mb_pos) && !menuBar()->childAt(mb_pos)) {
                    *result = HTCAPTION;
                    return true;
                }
            }
        }
    }
#else
    Q_UNUSED(event_type)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(event_type, message, result);
}

} // namespace dolphin::ui
