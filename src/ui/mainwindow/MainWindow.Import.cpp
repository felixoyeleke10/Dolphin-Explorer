// MainWindow.Import.cpp — onImportFile, showImportDialog.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/AppInfo.h"
#include "ui/shell/Features.h"
#include "ui/features/import/ImportReviewWizard.h"
#include "app/import/ImportClassifier.h"
#include "app/layers/LayerUtils.h"     // kModuleArtifactTypes (menu presets)
#include "app/project/Project.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

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
    return true;
}

void MainWindow::onImportFile()
{
    importFilesWithPreset({});
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
            if (m_import_ctrl) m_import_ctrl->importBatch(res.files);
    });

    wizard->show();
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
            if (m_import_ctrl) m_import_ctrl->importBatch(res.files);
    });

    wizard->show();
}

} // namespace dolphin::ui
