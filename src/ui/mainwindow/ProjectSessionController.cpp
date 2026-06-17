// ProjectSessionController.cpp — project lifecycle: new, open, save, close, auto-save.
#include "ui/mainwindow/ProjectSessionController.h"
#include "ui/mainwindow/NewProjectDialog.h"
#include "ui/bottom/DiagnosticsHub.h"
#include "ui/shell/AppInfo.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/services/ImportService.h"
#include "app/tasks/OperationManager.h"

#include <algorithm>
#include <cctype>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>

namespace {
constexpr int kMaxRecent = 8;
constexpr const char* kRecentKey = "recentProjects";
} // namespace

namespace dolphin::ui {

ProjectSessionController::ProjectSessionController(
    QUndoStack*            undo_stack,
    DiagnosticsHub*        diag_hub,
    app::OperationManager* op_mgr,
    app::ImportService*    import_service,
    QWidget*               dialog_parent,
    QObject*               parent)
    : QObject(parent)
    , m_undo_stack(undo_stack)
    , m_diag_hub(diag_hub)
    , m_op_mgr(op_mgr)
    , m_import_service(import_service)
    , m_dialog_parent(dialog_parent)
{}

// -- Public mutations --------------------------------------------------------

void ProjectSessionController::adoptNewProject(std::shared_ptr<app::Project> proj)
{
    m_project       = std::move(proj);
    m_project_dirty = false;
    m_project_load_gen++;
    emitWindowTitle();
}

void ProjectSessionController::markDirty()
{
    m_project_dirty = true;
    emitWindowTitle();
}

void ProjectSessionController::markClean()
{
    m_project_dirty = false;
    emitWindowTitle();
}

QStringList ProjectSessionController::recentProjects() const
{
    QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
    return s.value(kRecentKey).toStringList();
}

// -- Project CRUD slots ------------------------------------------------------

void ProjectSessionController::newProject()
{
    if (m_project && m_project_dirty && !m_project->isTempProject()) {
        const auto reply = QMessageBox::question(
            m_dialog_parent, tr("New Project"),
            tr("Save changes to \"%1\" before creating a new project?")
                .arg(QString::fromStdString(m_project->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            saveProject();
            if (m_project_dirty) return; // save was cancelled or failed
        }
    }

    NewProjectDialog dlg(m_dialog_parent);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString manifest = dlg.manifestPath();
    if (manifest.isEmpty()) return;

    emit projectAboutToChange();

    m_project_load_gen++;
    m_undo_stack->clear();
    m_project = app::Project::create(
        dlg.projectName().toStdString(), manifest.toStdString());
    if (!dlg.crs().empty())
        m_project->setCrs(dlg.crs().id);
    m_project_dirty = false;
    addToRecentProjects(manifest);
    emitWindowTitle();
    emit projectChanged(m_project);
    emit jobMessage("Project created: " + dlg.projectName());
}

void ProjectSessionController::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        m_dialog_parent, tr("Open"), QDir::homePath(),
        tr("All Dolphin Files (*.dlp *.pelagic *.dlpd *.dpcache);;"
           "Dolphin Project (*.dlp *.pelagic);;"
           "Survey Cache (*.dlpd *.dpcache)"));
    if (path.isEmpty()) return;

    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "dlpd" || ext == "dpcache") {
        // Cache-only file: hand back to MainWindow's import flow.
        // MainWindow connects to this signal and calls showImportDialog.
        emit jobMessage(QString("__import_cache__:") + path);
    } else {
        loadProjectPath(path.toStdString());
    }
}

void ProjectSessionController::openProjectPath(const std::string& path)
{
    loadProjectPath(path);
}

void ProjectSessionController::saveProject()
{
    if (!m_project) return;

    if (m_project->isTempProject() || m_project->manifestPath().empty()) {
        const QString path = QFileDialog::getSaveFileName(
            m_dialog_parent, tr("Save Project"), QDir::homePath(),
            tr("Dolphin Project (*.dlp)"));
        if (path.isEmpty()) return;
        if (m_project->saveAs(path.toStdString())) {
            m_project->setTempProject(false);
            markClean();
            if (m_diag_hub) {/*open-folder action enable handled by MainWindow*/}
            addToRecentProjects(path);
            emit jobMessage("Project saved as: " + QFileInfo(path).baseName());
        } else {
            const QString err = tr("Failed to save project: %1").arg(QFileInfo(path).baseName());
            if (m_diag_hub)
                m_diag_hub->postProblem(err, DiagnosticsHub::Severity::Error, "project");
            QMessageBox::warning(m_dialog_parent, tr("Save Project"),
                                 tr("Failed to save project."));
        }
        return;
    }

    if (m_project->save()) {
        markClean();
        emit jobMessage("Project saved.");
    } else {
        const QString err = tr("Failed to save project: %1")
            .arg(QString::fromStdString(m_project->name()));
        if (m_diag_hub)
            m_diag_hub->postProblem(err, DiagnosticsHub::Severity::Error, "project");
        QMessageBox::warning(m_dialog_parent, tr("Save Project"),
                             tr("Failed to save project."));
    }
}

void ProjectSessionController::saveProjectAs()
{
    if (!m_project) return;
    const QString path = QFileDialog::getSaveFileName(
        m_dialog_parent, tr("Save Project As"), QDir::homePath(),
        tr("Dolphin Project (*.dlp)"));
    if (path.isEmpty()) return;
    if (m_project->saveAs(path.toStdString())) {
        m_project->setTempProject(false);
        markClean();
        addToRecentProjects(path);
        emit jobMessage("Project saved as: " + QFileInfo(path).baseName());
    } else {
        const QString err = tr("Failed to save project: %1").arg(QFileInfo(path).baseName());
        if (m_diag_hub)
            m_diag_hub->postProblem(err, DiagnosticsHub::Severity::Error, "project");
        QMessageBox::warning(m_dialog_parent, tr("Save Project As"),
                             tr("Failed to save project."));
    }
}

void ProjectSessionController::openProjectFolder()
{
    if (!m_project) return;
    const std::string manifest = m_project->manifestPath();
    if (manifest.empty()) return;

    const QString folder = QFileInfo(QString::fromStdString(manifest)).absolutePath();
    if (!QDir(folder).exists()) {
        QMessageBox::warning(m_dialog_parent, tr("Open Project Folder"),
            tr("Project folder does not exist:\n") + folder);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void ProjectSessionController::closeProject()
{
    if (!m_project) return;

    if (!m_project->isTempProject() && m_project_dirty) {
        const auto reply = QMessageBox::question(
            m_dialog_parent, tr("Close Project"),
            tr("Save changes to \"%1\" before closing?")
                .arg(QString::fromStdString(m_project->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            saveProject();
            if (m_project_dirty) return; // save failed or cancelled
        }
    }

    emit projectAboutToChange();

    m_op_mgr->cancelAll();
    if (m_import_service) m_import_service->cancelPendingRebuild();
    m_project_load_gen++;
    m_undo_stack->clear();
    m_project.reset();
    m_project_dirty = false;
    emitWindowTitle();
    emit projectChanged(nullptr);
    emit jobMessage("Project closed.");
}

void ProjectSessionController::autoSave()
{
    if (!m_project || !m_project_dirty) return;
    if (m_project->isTempProject() || m_project->manifestPath().empty()) return;
    if (auto* w = QApplication::activeModalWidget();
        w && w->inherits("QFileDialog")) return;
    if (m_project->save()) {
        markClean();
    } else {
        emit jobMessage("Auto-save failed — check available disk space.");
    }
}

// -- Private helpers ---------------------------------------------------------

void ProjectSessionController::loadProjectPath(const std::string& path)
{
    // Guard against duplicate open (e.g. double-click on sidebar emits twice).
    if (!m_project_dirty && m_project && m_project->manifestPath() == path)
        return;

    if (m_project && m_project_dirty) {
        const auto reply = QMessageBox::question(
            m_dialog_parent, tr("Open Project"),
            m_project->isTempProject()
                ? tr("You have unsaved changes. Save before opening a project?")
                : tr("Save changes to \"%1\" before opening?")
                      .arg(QString::fromStdString(m_project->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            saveProject();
            if (m_project_dirty) return;
        }
    }

    emit projectAboutToChange();

    m_op_mgr->cancelAll();
    if (m_import_service) m_import_service->cancelPendingRebuild();
    m_project_load_gen++;
    m_undo_stack->clear();

    if (path.empty()) {
        m_project.reset();
        m_project_dirty = false;
        emitWindowTitle();
        emit projectChanged(nullptr);
        return;
    }

    m_project = app::Project::open(path);
    if (!m_project) {
        const QString qpath = QString::fromStdString(path);
        if (m_diag_hub)
            m_diag_hub->postProblem(tr("Could not open: %1").arg(qpath),
                                    DiagnosticsHub::Severity::Error, "project");
        // Re-enable viewport: projectAboutToChange suppressed it but no
        // projectChanged fired, so emit a null-project signal to trigger re-enable.
        emit projectChanged(nullptr);
        QMessageBox::warning(m_dialog_parent, tr("Open Project"),
            tr("Could not open: %1").arg(qpath));
        return;
    }

    m_project_dirty = false;
    addToRecentProjects(QString::fromStdString(path));
    emitWindowTitle();
    emit projectChanged(m_project);
    emit jobMessage(QString("Opened: %1")
        .arg(QString::fromStdString(m_project->name())));

    // Defer layer pre-population to the next event-loop tick.
    // Calling activateLayer() synchronously triggers Win32 message processing
    // mid-call-stack on Windows, which can fault against partially-initialised
    // receivers.  Deferring lets loadProjectPath() unwind fully first.
    const uint64_t load_gen = m_project_load_gen;
    QTimer::singleShot(0, this, [this, load_gen]() {
        if (!m_project || load_gen != m_project_load_gen) return;

        // Find the first indexed layer to return to MainWindow for selection.
        std::string first_layer_id;
        for (const auto& layer : m_project->layers()) {
            if (!layer) continue;
            if (layer->index_built && !layer->artifact_index.empty()) {
                first_layer_id = layer->id;
                break;
            }
        }

        // Background-reindex layers whose artifact index was not embedded in
        // the JSON manifest (deferred by fromJson for fast project-open).
        if (m_import_service) {
            for (const auto& layer : m_project->layers()) {
                if (!layer || layer->index_built) continue;
                if (layer->artifact_store_path.empty()) continue;
                const std::string sfmt = [&] {
                    std::string f = layer->artifact_store_format;
                    for (auto& c : f) c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                    return f;
                }();
                if (sfmt != "dlpd" && sfmt != "dpcache") continue;
                m_import_service->rebuildCacheIndex(layer->id, m_project);
            }
        }

        emit firstLayerReady(first_layer_id);
    });
}

void ProjectSessionController::addToRecentProjects(const QString& path)
{
    if (path.isEmpty()) return;
    QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
    QStringList list = s.value(kRecentKey).toStringList();
    list.removeAll(path);
    list.prepend(path);
    if (list.size() > kMaxRecent) list.resize(kMaxRecent);
    s.setValue(kRecentKey, list);
    emit recentProjectsChanged(list);
}

void ProjectSessionController::emitWindowTitle()
{
    emit windowTitleChanged(buildWindowTitle());
}

QString ProjectSessionController::buildWindowTitle() const
{
    if (!m_project) return tr("Dolphin Explorer");
    QString title;
    if (m_project_dirty) title += QStringLiteral("• "); // • prefix for unsaved
    title += QString::fromStdString(m_project->name());
    if (m_project->isTempProject()) title += tr(" (unsaved)");
    title += " — Dolphin Explorer"; // em dash
    return title;
}

} // namespace dolphin::ui
