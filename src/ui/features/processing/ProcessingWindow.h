#pragma once
#include <QMainWindow>
#include <memory>
#include <string>
#include <vector>
#include "ui/features/map/MapTypes.h"

class QAction;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QToolBar;

namespace dolphin::app {
class Project;
class ProcessingService;
}

namespace dolphin::ui {
class SidescanViewController;

// Processing Window — dedicated window for running the node-graph pipeline and
// pre-building display caches (map previews at all quality tiers) so that
// MapView and WaterfallWindow are freed from heavy processing work.
//
// "Run Pipeline"     — delegates to ProcessingService (node graph only).
// "Build Previews"   — calls SidescanViewController::prebuildTier for each
//                      quality tier so quality changes become instant.
class ProcessingWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ProcessingWindow(QWidget* parent = nullptr);
    ~ProcessingWindow() override = default;

    void setProject(std::shared_ptr<app::Project>  project,
                    app::ProcessingService*          proc_service,
                    SidescanViewController*          sss_ctrl);

public slots:
    // Called when a tier pre-build completes — updates the status column.
    void onPrebuildTierComplete(const std::string& layer_id, MapSonarQuality quality);

    // Called by ProcessingService signals.
    void onRunStarted (const std::string& layer_id);
    void onRunComplete(const std::string& layer_id, const std::string& summary);
    void onRunFailed  (const std::string& layer_id, const std::string& error);
    void onBatchProgress(int done, int total);
    void onBatchComplete(int succeeded, int total);

private slots:
    void onBuildPreviews();
    void onRunPipeline();
    void onRunAll();

private:
    void buildUi();
    void refreshLayerList();
    void updateLayerTierStatus(const std::string& layer_id);
    void appendLog(const QString& msg);
    std::vector<std::string> selectedLayerIds() const;

    std::shared_ptr<app::Project> m_project;
    app::ProcessingService*       m_proc_service = nullptr;
    SidescanViewController*       m_sss_ctrl     = nullptr;

    QTreeWidget*  m_layer_list  = nullptr;
    QTextEdit*    m_log         = nullptr;
    QProgressBar* m_progress    = nullptr;
    QLabel*       m_status_lbl  = nullptr;

    QPushButton*  m_btn_build   = nullptr;
    QPushButton*  m_btn_run     = nullptr;
    QPushButton*  m_btn_run_all = nullptr;

};

} // namespace dolphin::ui
