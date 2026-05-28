// MainWindow.Import.cpp — onImportFile, showImportDialog.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/Features.h"
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/features/import/ImportSetupDialog.h"
#include "app/project/Project.h"

#include <QDateTime>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

namespace dolphin::ui {

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
            this, [this](const ImportDialogResult& res) {
        if (!ensureProjectForImport(res)) return;
        if constexpr (Features::kImport)
            if (m_import_ctrl)
                m_import_ctrl->importBatch(res.files);
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
            this, [this](const ImportDialogResult& res) {
        if (!ensureProjectForImport(res)) return;
        if constexpr (Features::kImport)
            if (m_import_ctrl)
                m_import_ctrl->importBatch(res.files);
    });

    wizard->show();
}

} // namespace dolphin::ui
