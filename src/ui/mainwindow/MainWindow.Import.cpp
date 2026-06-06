// MainWindow.Import.cpp — onImportFile, showImportDialog.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/Features.h"
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/features/import/ImportSetupDialog.h"
#include "app/import/ImportClassifier.h"
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
        const auto fresh = app::classifyImportAction(action.path, project);
        action.kind               = fresh.kind;
        action.existing_layer_id  = fresh.existing_layer_id;
        action.existing_source_id = fresh.existing_source_id;
    }
}

bool MainWindow::ensureProjectForImport(const ImportDialogResult& res)
{
    if (!res.accepted || res.files.isEmpty()) return false;

    if (!res.source_crs.empty())
        m_pending_crs = res.source_crs;

    if (res.target == ImportDialogResult::ProjectTarget::New) {
        const QString folder = res.new_project_folder;
        if (!QDir().mkpath(folder)) {
            QMessageBox::warning(this, tr("Import"),
                tr("Could not create project folder:\n") + folder);
            return false;
        }
        const QString proj_path = folder + "/" + res.new_project_name + ".dlp";
        m_project = app::Project::create(
            res.new_project_name.toStdString(), proj_path.toStdString());
        if (!m_project) {
            QMessageBox::warning(this, tr("Import"),
                tr("Failed to create project."));
            return false;
        }
        addToRecentProjects(proj_path);
        bindProjectUi();
    } else if (!m_project) {
        // Check whether an existing managed project already holds these files.
        const QString existing = findManagedProjectForPaths(res.files);
        if (!existing.isEmpty()) {
            const QString proj_name = QFileInfo(existing).dir().dirName();
            const auto reply = QMessageBox::question(this, tr("Existing Project Found"),
                tr("A project already contains this data:\n\n%1\n\nOpen it?")
                    .arg(proj_name),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                loadProject(existing.toStdString());
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
        m_project = app::Project::create(
            session_name.toStdString(), proj_path.toStdString());
        m_project->setTempProject(true);
        bindProjectUi();
    }
    return true;
}

void MainWindow::onImportFile()
{
    // Step 1 — sensor type selection.
    ImportSetupDialog setup(this);
    if (setup.exec() != QDialog::Accepted) return;
    const auto module_filter = setup.moduleFilter();

    // Step 2 — review wizard (seeded with last-used CRS + sensor filter).
    auto* wizard = new ImportReviewWizard(m_project.get(), m_pending_crs, this);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    wizard->setModuleFilter(module_filter);

    connect(wizard, &ImportReviewWizard::importConfirmed,
            this, [this](ImportDialogResult res) {
        if (!ensureProjectForImport(res)) return;
        // Re-classify against the now-open project; if ensureProject opened an
        // existing project the wizard's ImportNew entries become Reuse/Rebuild.
        reclassify(res.files, m_project.get());
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

    auto* wizard = new ImportReviewWizard(m_project.get(), m_pending_crs, this);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    wizard->addFiles(paths);

    connect(wizard, &ImportReviewWizard::importConfirmed,
            this, [this](ImportDialogResult res) {
        if (!ensureProjectForImport(res)) return;
        reclassify(res.files, m_project.get());
        if constexpr (Features::kImport)
            if (m_import_ctrl) m_import_ctrl->importBatch(res.files);
    });

    wizard->show();
}

} // namespace dolphin::ui
