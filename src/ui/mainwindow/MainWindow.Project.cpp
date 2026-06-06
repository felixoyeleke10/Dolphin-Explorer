// MainWindow.Project.cpp — project CRUD: new, open, save, close, load.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/AppInfo.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/services/ImportService.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

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
#include <QUrl>

namespace {
constexpr int kMaxRecent = 8;
constexpr const char* kRecentKey = "recentProjects";
} // namespace

namespace dolphin::ui {

void MainWindow::addToRecentProjects(const QString& path)
{
    if (path.isEmpty()) return;
    QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
    QStringList list = s.value(kRecentKey).toStringList();
    list.removeAll(path);
    list.prepend(path);
    if (list.size() > kMaxRecent) list.resize(kMaxRecent);
    s.setValue(kRecentKey, list);
    refreshSidebarSections(list);
    rebuildRecentMenu();
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
        const QString name = QFileInfo(path).baseName();
        auto* act = m_recent_menu->addAction(name, this, [this, path]() {
            loadProject(path.toStdString());
        });
        act->setToolTip(path);
    }
    m_recent_menu->addSeparator();
    m_recent_menu->addAction(tr("Clear Recent"), this, [this]() {
        QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
        s.remove(kRecentKey);
    });
}

void MainWindow::onNewProject()
{
    if (m_project && m_project_dirty && !m_project->isTempProject()) {
        const auto reply = QMessageBox::question(
            this, tr("New Project"),
            tr("Save changes to \"%1\" before creating a new project?")
                .arg(QString::fromStdString(m_project->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            onSaveProject();
            if (m_project_dirty) return;  // save was cancelled or failed
        }
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("New Project"), QDir::homePath(),
        tr("Dolphin Project (*.dlp)"));
    if (path.isEmpty()) return;

    if (m_sss_ctrl) m_sss_ctrl->deactivate(true);
    m_active_layer_id.clear();
    clearNavigationHistory();

    m_project = app::Project::create(
        QFileInfo(path).baseName().toStdString(), path.toStdString());
    addToRecentProjects(path);
    bindProjectUi();
    appendJobMessage("Project created.");
}

void MainWindow::onOpenProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open"), QDir::homePath(),
        tr("All Dolphin Files (*.dlp *.pelagic *.dlpd *.dpcache);;"
           "Dolphin Project (*.dlp *.pelagic);;"
           "Survey Cache (*.dlpd *.dpcache)"));
    if (path.isEmpty()) return;

    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "dlpd" || ext == "dpcache")
        showImportDialog({path});
    else
        loadProject(path.toStdString());
}

void MainWindow::onSaveProject()
{
    if (!m_project) return;

    if (m_project->isTempProject() || m_project->manifestPath().empty()) {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save Project"), QDir::homePath(),
            tr("Dolphin Project (*.dlp)"));
        if (path.isEmpty()) return;
        if (m_project->saveAs(path.toStdString())) {
            m_project->setTempProject(false);
            m_project_dirty = false;
            setWindowTitleFromProject();
            if (m_act_open_folder) m_act_open_folder->setEnabled(true);
            addToRecentProjects(path);
            appendJobMessage("Project saved as: " + QFileInfo(path).baseName());
            recordActivity(ActivityKind::Import,
                tr("Project saved: %1").arg(QFileInfo(path).baseName()));
        } else {
            const QString err = tr("Failed to save project: %1").arg(QFileInfo(path).baseName());
            m_diag_hub->postProblem(err, DiagnosticsHub::Severity::Error, "project");
            QMessageBox::warning(this, tr("Save Project"), tr("Failed to save project."));
        }
        return;
    }

    if (m_project->save()) {
        m_project_dirty = false;
        setWindowTitleFromProject();
        appendJobMessage("Project saved.");
        recordActivity(ActivityKind::Import,
            tr("Project saved: %1").arg(QString::fromStdString(m_project->name())));
    } else {
        const QString err = tr("Failed to save project: %1")
            .arg(QString::fromStdString(m_project->name()));
        m_diag_hub->postProblem(err, DiagnosticsHub::Severity::Error, "project");
        QMessageBox::warning(this, tr("Save Project"), tr("Failed to save project."));
    }
}

void MainWindow::onSaveProjectAs()
{
    if (!m_project) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), QDir::homePath(),
        tr("Dolphin Project (*.dlp)"));
    if (path.isEmpty()) return;
    if (m_project->saveAs(path.toStdString())) {
        m_project->setTempProject(false);
        m_project_dirty = false;
        setWindowTitleFromProject();
        if (m_act_open_folder) m_act_open_folder->setEnabled(true);
        addToRecentProjects(path);
        appendJobMessage("Project saved as: " + QFileInfo(path).baseName());
    } else {
        const QString err = tr("Failed to save project: %1").arg(QFileInfo(path).baseName());
        m_diag_hub->postProblem(err, DiagnosticsHub::Severity::Error, "project");
        QMessageBox::warning(this, tr("Save Project As"), tr("Failed to save project."));
    }
}

void MainWindow::onOpenProjectFolder()
{
    if (!m_project) return;
    const std::string manifest = m_project->manifestPath();
    if (manifest.empty()) return;

    const QString folder = QFileInfo(QString::fromStdString(manifest)).absolutePath();
    if (!QDir(folder).exists()) {
        QMessageBox::warning(this, tr("Open Project Folder"),
            tr("Project folder does not exist:\n") + folder);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::onCloseProject()
{
    if (!m_project) return;

    if (!m_project->isTempProject() && m_project_dirty) {
        const auto reply = QMessageBox::question(
            this, tr("Close Project"),
            tr("Save changes to \"%1\" before closing?")
                .arg(QString::fromStdString(m_project->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            onSaveProject();
            if (m_project_dirty) return;  // save was cancelled or failed
        }
    }

    // Suppress map repaints during the clear+bind so the viewport doesn't flash
    // a blank frame between the old project being cleared and the empty state.
    if (m_viewport_host) m_viewport_host->setUpdatesEnabled(false);

    m_op_mgr->cancelAll();
    if (m_sss_ctrl) m_sss_ctrl->deactivate(true);
    if (m_import_service) m_import_service->cancelPendingRebuild();
    m_active_layer_id.clear();
    clearNavigationHistory();
    ++m_project_load_gen;
    m_undo_stack->clear();
    m_project.reset();
    bindProjectUi();

    if (m_viewport_host) m_viewport_host->setUpdatesEnabled(true);
    appendJobMessage("Project closed.");
}

void MainWindow::loadProject(const std::string& path)
{
    // Double-click on sidebar emits itemClicked twice → two deferred loadProject calls.
    // Short-circuit if this project is already cleanly open.
    if (!m_project_dirty && m_project
            && m_project->manifestPath() == path)
        return;

    if (m_project && m_project_dirty) {
        const auto reply = QMessageBox::question(
            this, tr("Open Project"),
            m_project->isTempProject()
                ? tr("You have unsaved changes. Save before opening a project?")
                : tr("Save changes to \"%1\" before opening?")
                      .arg(QString::fromStdString(m_project->name())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            onSaveProject();
            if (m_project_dirty) return;
        }
    }

    // Suppress map repaints for the entire clear→bind→first-activation sequence.
    // Without this the viewport flashes a blank frame between deactivate() clearing
    // old layer data and the deferred first-layer activation on the next tick.
    if (m_viewport_host) m_viewport_host->setUpdatesEnabled(false);

    m_op_mgr->cancelAll();
    if (m_import_service) m_import_service->cancelPendingRebuild();
    if (m_sss_ctrl) m_sss_ctrl->deactivate(true);
    m_active_layer_id.clear();
    clearNavigationHistory();
    ++m_project_load_gen;
    m_undo_stack->clear();

    if (path.empty()) {
        m_project.reset();
        bindProjectUi();
        if (m_viewport_host) m_viewport_host->setUpdatesEnabled(true);
        return;
    }

    m_project = app::Project::open(path);
    if (!m_project) {
        const QString qpath = QString::fromStdString(path);
        m_diag_hub->postProblem(tr("Could not open: %1").arg(qpath),
                                DiagnosticsHub::Severity::Error, "project");
        if (m_viewport_host) m_viewport_host->setUpdatesEnabled(true);
        QMessageBox::warning(this, tr("Open Project"),
            tr("Could not open: %1").arg(qpath));
        return;
    }
    addToRecentProjects(QString::fromStdString(path));
    bindProjectUi();
    appendJobMessage(QString("Opened: %1")
        .arg(QString::fromStdString(m_project->name())));

    // Defer layer pre-population to the next event-loop tick.
    // Calling activateLayer() synchronously here emits loadingStarted which calls
    // setVisible() on the status-bar progress widget.  On Windows that synchronous
    // ShowWindow triggers Win32 message processing mid-call-stack, which can dispatch
    // queued Qt events against partially-initialised receivers and fault inside
    // QMetaObject::activate.  Deferring lets loadProject() unwind fully first.
    const uint64_t load_gen = m_project_load_gen;
    QTimer::singleShot(0, this, [this, load_gen]() {
        // Re-enable updates at this point regardless of whether layers activate:
        // the bind phase is complete and the viewport should reflect current state.
        if (m_viewport_host) m_viewport_host->setUpdatesEnabled(true);

        if (!m_project || load_gen != m_project_load_gen) return;
        using M = app::Modality;
        std::string first_layer_id;
        std::string first_sss_id;
        for (const auto& layer : m_project->layers()) {
            if (!layer || !layer->index_built || layer->artifact_index.empty()) continue;
            if (layer->modality == M::Sidescan) {
                if (m_sss_ctrl && first_sss_id.empty()) {
                    m_sss_ctrl->activateLayer(layer->id, m_project.get());
                    first_sss_id = layer->id;
                }
            } else if (m_map_view) {
                m_map_view->setActiveLayer(layer->id);
                m_map_view->setNavTrackVisible(layer->id, true);
            }
            if (first_layer_id.empty())
                first_layer_id = layer->id;
        }
        if (m_viewport_host) m_viewport_host->setActiveLayer({});

        // Select the first layer so m_active_layer_id is populated, the inspector
        // shows the layer, and the waterfall (if already open) loads it immediately.
        if (!first_layer_id.empty())
            onLayerSelected(first_layer_id);

            // Background-reindex layers that have a valid .dlpd cache but whose
        // artifact index was not embedded in the JSON (deferred by fromJson).
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

        // Auto-trigger processing for any indexed layer that has not yet been
        // run through the pipeline. Skip if all layers are already processed to
        // avoid re-applying corrections on data that was already corrected.
        bool any_needs_processing = false;
        for (const auto& layer : m_project->layers()) {
            if (layer && layer->index_built && !layer->pipeline_applied
                    && layer->modality == M::Sidescan) {
                any_needs_processing = true;
                break;
            }
        }
        if (any_needs_processing)
            triggerAutoProcessing();
    });
}

void MainWindow::onRemoveContact(uint64_t contact_id)
{
    if (!m_project) return;
    const auto& contacts = m_project->contacts();
    const auto it = std::find_if(contacts.begin(), contacts.end(),
        [contact_id](const core::Contact& c) { return c.id == contact_id; });
    if (it == contacts.end()) return;
    const QString label = QString::fromStdString(it->label);
    m_undo_stack->push(new RemoveContactCommand(
        m_project.get(), *it,
        [this]() { m_project_dirty = true; setWindowTitleFromProject(); }));
    recordActivity(ActivityKind::ContactPick, tr("Contact %1 removed").arg(label));
}

void MainWindow::onRevealSource(const std::string& source_id)
{
    if (!m_project) return;
    const auto* src = m_project->findSource(source_id);
    if (!src) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(QString::fromStdString(src->path)).dir().absolutePath()));
}

void MainWindow::onAutoSave()
{
    if (!m_project || !m_project_dirty) return;
    if (m_project->isTempProject() || m_project->manifestPath().empty()) return;
    // Only skip if a save-file dialog is open — other modals (Settings, Import)
    // don't conflict with a silent file write.
    if (auto* w = QApplication::activeModalWidget();
        w && w->inherits("QFileDialog")) return;
    if (m_project->save()) {
        m_project_dirty = false;
        setWindowTitleFromProject();
    } else {
        m_app_state->postNotification({
            ui::NotificationSeverity::Error,
            "Auto-save failed — check available disk space.",
            "AutoSave"
        });
    }
}

} // namespace dolphin::ui
