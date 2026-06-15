#pragma once
#include "app/project/Project.h"
#include "core/SpatialRef.h"

#include <QObject>
#include <QStringList>
#include <memory>
#include <string>

class QUndoStack;
class QWidget;

namespace dolphin::app { class ImportService; class OperationManager; }
namespace dolphin::ui  { class DiagnosticsHub; }

namespace dolphin::ui {

// Owns all project-lifecycle state: the live Project pointer, dirty flag,
// and the CRUD operations (new / open / save / close / auto-save).
//
// Emits signals so MainWindow can react without embedding project policy.
// The UI (viewport suppression, layer activation, widget binding) stays in
// MainWindow and responds to signals declared here.
class ProjectSessionController : public QObject {
    Q_OBJECT
public:
    explicit ProjectSessionController(
        QUndoStack*            undo_stack,
        DiagnosticsHub*        diag_hub,
        app::OperationManager* op_mgr,
        app::ImportService*    import_service,
        QWidget*               dialog_parent,
        QObject*               parent = nullptr);

    // -- Read-only accessors -------------------------------------------------
    std::shared_ptr<app::Project> project()    const { return m_project; }
    bool                          isDirty()    const { return m_project_dirty; }
    const core::SpatialRef&       pendingCrs() const { return m_pending_crs; }

    // -- Mutations -----------------------------------------------------------
    // Mark dirty + emit windowTitleChanged. Call from any MainWindow aspect
    // that mutates project state (layer visibility, palette, etc.).
    void markDirty();
    // Mark clean + emit windowTitleChanged. Called by PSC save paths internally;
    // exposed so MainWindow can call after a successful external save.
    void markClean();

    void setPendingCrs(const core::SpatialRef& crs) { m_pending_crs = crs; }

    // Used by the import flow to inject a freshly-created project without the
    // full open/close ceremony (no projectAboutToChange / viewport suppression).
    // Caller must call addToRecentProjects and bindProjectUi() manually after.
    void adoptNewProject(std::shared_ptr<app::Project> proj);

    // -- Recent projects -----------------------------------------------------
    QStringList recentProjects() const;

public slots:
    void newProject();
    void openProject();
    void openProjectPath(const std::string& path);
    void saveProject();
    void saveProjectAs();
    void openProjectFolder();
    void closeProject();
    void autoSave();

signals:
    // Fired just before the project pointer changes (open, new, close).
    // MainWindow uses this to suppress viewport updates and deactivate SSS.
    void projectAboutToChange();

    // Fired after the project pointer is set (including to null for close).
    void projectChanged(std::shared_ptr<app::Project> project);

    // Fired (deferred, next event-loop tick) after a project load completes.
    // `first_layer_id` is the first indexed layer; empty if project has none.
    // MainWindow re-enables viewport and activates layers in this handler.
    void firstLayerReady(const std::string& first_layer_id);

    // Window title needs updating (call QMainWindow::setWindowTitle).
    void windowTitleChanged(const QString& title);

    // Recent-project list changed; refresh sidebar + recent menu.
    void recentProjectsChanged(const QStringList& paths);

    // Proxy for MainWindow::appendJobMessage — avoids a direct back-reference.
    void jobMessage(const QString& message);

private:
    void loadProjectPath(const std::string& path);
    void addToRecentProjects(const QString& path);
    void emitWindowTitle();
    QString buildWindowTitle() const;

    QUndoStack*            m_undo_stack;
    DiagnosticsHub*        m_diag_hub;
    app::OperationManager* m_op_mgr;
    app::ImportService*    m_import_service;
    QWidget*               m_dialog_parent;

    std::shared_ptr<app::Project> m_project;
    bool             m_project_dirty    = false;
    uint64_t         m_project_load_gen = 0;
    bool             m_save_in_progress = false;
    core::SpatialRef m_pending_crs;
};

} // namespace dolphin::ui
