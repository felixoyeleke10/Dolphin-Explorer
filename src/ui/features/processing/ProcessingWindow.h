#pragma once
#include <QMainWindow>
#include <memory>
#include <string>
#include <vector>

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

class ProcessingWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ProcessingWindow(QWidget* parent = nullptr);
    ~ProcessingWindow() override = default;

    void setProject(std::shared_ptr<app::Project> project,
                    app::ProcessingService*         proc_service);

public slots:
    void onRunStarted (const std::string& layer_id);
    void onRunComplete(const std::string& layer_id, const std::string& summary);
    void onRunFailed  (const std::string& layer_id, const std::string& error);
    void onBatchProgress(int done, int total);
    void onBatchComplete(int succeeded, int total);

private slots:
    void onRunPipeline();
    void onRunAll();

private:
    void buildUi();
    void refreshLayerList();
    void appendLog(const QString& msg);
    std::vector<std::string> selectedLayerIds() const;

    std::shared_ptr<app::Project> m_project;
    app::ProcessingService*       m_proc_service = nullptr;

    QTreeWidget*  m_layer_list  = nullptr;
    QTextEdit*    m_log         = nullptr;
    QProgressBar* m_progress    = nullptr;
    QLabel*       m_status_lbl  = nullptr;

    QPushButton*  m_btn_run     = nullptr;
    QPushButton*  m_btn_run_all = nullptr;
};

} // namespace dolphin::ui
