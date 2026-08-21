// MainWindow.Import.cpp — onImportFile, showImportDialog.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/AppInfo.h"
#include "ui/shell/Features.h"
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/features/import/ImportSetupDialog.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/shared/panels/LineListPanel.h"
#include "app/import/ImportClassifier.h"
#include "app/layers/LayerUtils.h"     // kModuleArtifactTypes (menu presets)
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "io/ProbeDispatch.h"          // fileFilterForArtifactType
#include "io/raster/RasterReader.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

#include <algorithm>

namespace dolphin::ui {

// Scan the app-managed projects directory for any .dlp manifest that already
// contains one of the given source paths.  Returns the manifest path of the
// first match, or empty string if none found.
static QString findManagedProjectForPaths(const QList<FileImportAction>& files)
{
    const QString proj_root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/projects/";

    for (const QFileInfo& dir_entry :
             QDir(proj_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString manifest = dir_entry.filePath() + "/" + dir_entry.fileName() + ".dlp";
        if (!QFileInfo::exists(manifest)) continue;

        auto proj = app::Project::open(manifest.toStdString());
        if (!proj) continue;

        for (const auto& action : files) {
            if (proj->findSourceByPath(action.path.toStdString()))
                return manifest;
        }
    }
    return {};
}

// Re-classify all actions against the now-open project so stale ImportNew
// entries (computed before the project was known) pick up Reuse/Rebuild.
static void reclassify(QList<FileImportAction>& files, const app::Project* project)
{
    if (!project) return;
    for (auto& action : files) {
        // Re-classify against the now-open project, keeping the action's chosen
        // module(s) so the decision stays modality-aware — otherwise a requested
        // modality missing from an already-imported source would be downgraded to
        // ReuseExisting and its layer never created.
        const auto fresh = app::classifyImportAction(action.path, project,
                                                     action.module_filter);
        action.kind               = fresh.kind;
        action.existing_layer_id  = fresh.existing_layer_id;
        action.existing_source_id = fresh.existing_source_id;
    }
}

bool MainWindow::ensureProjectForImport(const ImportDialogResult& res)
{
    if (!res.accepted || res.files.isEmpty()) return false;

    if (!res.source_crs.empty())
        m_session_ctrl->setPendingCrs(res.source_crs);

    if (res.target == ImportDialogResult::ProjectTarget::New) {
        const QString folder = res.new_project_folder;
        if (!QDir().mkpath(folder)) {
            QMessageBox::warning(this, tr("Import"),
                tr("Could not create project folder:\n") + folder);
            return false;
        }
        const QString proj_path = folder + "/" + res.new_project_name + ".dlp";
        auto new_proj = app::Project::create(
            res.new_project_name.toStdString(), proj_path.toStdString());
        if (!new_proj) {
            QMessageBox::warning(this, tr("Import"),
                tr("Failed to create project."));
            return false;
        }
        m_session_ctrl->adoptNewProject(std::move(new_proj));
        // Re-use PSC's addToRecentProjects via the public recentProjectsChanged signal
        // by calling openProjectPath with the manifest — but here we just wire the
        // recent entry manually since the project is already in PSC.
        // NOTE: call rebuildRecentMenu() after so the sidebar reflects the new entry.
        {
            QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
            QStringList list = s.value("recentProjects").toStringList();
            list.removeAll(proj_path);
            list.prepend(proj_path);
            if (list.size() > 8) list.resize(8);
            s.setValue("recentProjects", list);
            refreshSidebarSections(list);
            rebuildRecentMenu();
        }
        bindProjectUi();
    } else if (!currentProject()) {
        // Check whether an existing managed project already holds these files.
        const QString existing = findManagedProjectForPaths(res.files);
        if (!existing.isEmpty()) {
            const QString proj_name = QFileInfo(existing).dir().dirName();
            const auto reply = QMessageBox::question(this, tr("Existing Project Found"),
                tr("A project already contains this data:\n\n%1\n\nOpen it?")
                    .arg(proj_name),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                m_session_ctrl->openProjectPath(existing.toStdString());
                return true;
            }
        }

        // No existing managed project — create a new session project.
        createSessionProject();
    }
    return true;
}

// Create + adopt a temporary session project (used when importing without an open
// project). Shared by the sonar import flow and the raster import path.
void MainWindow::createSessionProject()
{
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    const QString session_name = "Session_" + ts;
    const QString root_dir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/projects/" + session_name;
    QDir().mkpath(root_dir);
    const QString proj_path = root_dir + "/" + session_name + ".dlp";
    auto sess_proj = app::Project::create(
        session_name.toStdString(), proj_path.toStdString());
    if (sess_proj) sess_proj->setTempProject(true);
    m_session_ctrl->adoptNewProject(std::move(sess_proj));
    bindProjectUi();
}

void MainWindow::onImportFile()
{
    // Step 1 — sensor-type window. Picks the modality up front; the review wizard
    // then detects each file's contents and lets the user confirm/adjust per file.
    ImportSetupDialog setup(this);
    if (setup.exec() != QDialog::Accepted) return;
    const auto preset = setup.moduleFilter();
    // Rasters are georeferenced grids/images, not ping streams — they take a
    // dedicated GDAL path, not the sonar detect-then-confirm wizard.
    if (preset.size() == 1 && preset.front() == core::ArtifactType::Raster) {
        importRasterFiles();
        return;
    }
    importFilesWithPreset(preset);
}

// Detect-then-confirm import. No blind "what are you importing?" step: the wizard
// probes each file, shows the modalities it actually contains, and lets the user
// confirm which to import per file. `preset` (from a modality-specific menu command)
// pre-checks that family; empty = pre-check everything detected.
void MainWindow::importFilesWithPreset(const std::vector<core::ArtifactType>& preset)
{
    auto* wizard = new ImportReviewWizard(currentProject(), m_session_ctrl->pendingCrs(), this);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    if (!preset.empty()) wizard->setModuleFilter(preset);

    connect(wizard, &ImportReviewWizard::importConfirmed,
            this, [this](ImportDialogResult res) {
        if (!ensureProjectForImport(res)) return;
        // Re-classify against the now-open project; if ensureProject opened an
        // existing project the wizard's ImportNew entries become Reuse/Rebuild.
        reclassify(res.files, currentProject());
        if constexpr (Features::kImport)
            if (m_import_overlay && m_viewport_host)
                m_import_overlay->attachTo(m_viewport_host);
            if (m_import_ctrl) m_import_ctrl->importBatch(res.files);
    });

    wizard->show();
}

// Dedicated raster import: probe each file with GDAL, classify depth vs visual,
// and create a first-class Raster layer (the source file IS the durable store —
// read on activation). Bypasses the sonar ping pipeline entirely.
void MainWindow::importRasterFiles()
{
    const QString filter = QString::fromStdString(
        io::fileFilterForArtifactType(core::ArtifactType::Raster));
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Import Raster"), QString(), filter);
    if (paths.isEmpty()) return;

    if (!currentProject()) createSessionProject();
    if (!currentProject()) return;

    int added = 0;
    std::string last_id;
    for (const QString& qpath : paths) {
        const std::string path = qpath.toStdString();
        const auto info = io::probeRaster(path);
        if (!info) {
            appendJobMessage(tr("Raster import failed (unreadable): %1")
                                 .arg(QFileInfo(qpath).fileName()));
            continue;
        }

        auto* src = currentProject()->addSource(path, "geotiff");
        if (!src) continue;
        auto* layer = currentProject()->addLayer(
            src->id, QFileInfo(qpath).completeBaseName().toStdString());
        if (!layer) continue;

        layer->modality             = app::Modality::Raster;
        layer->artifact_store_path  = path;
        layer->artifact_store_format = "geotiff";
        layer->index_built          = true;

        auto& rm = layer->raster;
        rm.valid    = true;
        rm.is_depth = (info->kind == io::RasterKind::Elevation);
        rm.cols     = info->cols;
        rm.rows     = info->rows;
        rm.crs_wkt  = info->crs_wkt;
        for (int i = 0; i < 6; ++i) rm.geo_transform[i] = info->geo_transform[i];
        // Extent from the four geo-transform corners (source CRS units).
        const double* g = rm.geo_transform;
        auto wx = [&](double c, double r){ return g[0] + c*g[1] + r*g[2]; };
        auto wy = [&](double c, double r){ return g[3] + c*g[4] + r*g[5]; };
        const double xs[4] = { wx(0,0), wx(rm.cols,0), wx(0,rm.rows), wx(rm.cols,rm.rows) };
        const double ys[4] = { wy(0,0), wy(rm.cols,0), wy(0,rm.rows), wy(rm.cols,rm.rows) };
        rm.min_x = *std::min_element(xs, xs+4); rm.max_x = *std::max_element(xs, xs+4);
        rm.min_y = *std::min_element(ys, ys+4); rm.max_y = *std::max_element(ys, ys+4);

        currentProject()->commitLayer(layer->id);   // state → Ready, announces it
        last_id = layer->id;
        ++added;
        recordActivity(ActivityKind::Import,
            tr("Imported raster: %1 (%2)")
                .arg(QString::fromStdString(layer->label),
                     rm.is_depth ? tr("depth") : tr("image")));
    }

    if (added == 0) return;
    if (m_line_list) m_line_list->refresh();
    refreshInspectorModalities();
    if (!last_id.empty()) onLayerSelected(last_id);
    appendJobMessage(tr("Imported %n raster layer(s)", nullptr, added));
}

void MainWindow::showImportDialog(const QStringList& paths,
                                   const std::vector<core::ArtifactType>&)
{
    // Legacy entry point (drag-drop from OS shell).
    if (paths.isEmpty()) return;

    auto* wizard = new ImportReviewWizard(currentProject(), m_session_ctrl->pendingCrs(), this);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    wizard->addFiles(paths);

    connect(wizard, &ImportReviewWizard::importConfirmed,
            this, [this](ImportDialogResult res) {
        if (!ensureProjectForImport(res)) return;
        reclassify(res.files, currentProject());
        if constexpr (Features::kImport)
            if (m_import_overlay && m_viewport_host)
                m_import_overlay->attachTo(m_viewport_host);
            if (m_import_ctrl) m_import_ctrl->importBatch(res.files);
    });

    wizard->show();
}

} // namespace dolphin::ui
