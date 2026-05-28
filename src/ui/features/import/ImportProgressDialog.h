#pragma once
#include <QDialog>
#include <QString>
#include <string>
#include <vector>

class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace dolphin::ui {

// Replaces ImportProgressOverlay.
//
// A proper top-level dialog window showing batch import progress.
// Opens automatically on addJob(), stays open until the user dismisses it
// (or clicks "Run in Background" to hide without cancelling).
//
// Public API is intentionally identical to the old overlay so ImportController
// needs no logic changes.
class ExecutionProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExecutionProgressDialog(QWidget* parent = nullptr);

    void setQueueTotal(int n);

    void addJob(const std::string& layer_id,
                const QString&     filename,
                const QString&     format,
                float              size_mb);

    void updateJob(const std::string& layer_id, int percent);

    void finishJob(const std::string& layer_id,
                   int                artifact_count,
                   float              freq_khz,
                   const QString&     coord_sys);

    // Simplified overload for processing jobs (no artifact/freq metadata).
    void finishJob(const std::string& layer_id, const QString& result_text);

    void failJob(const std::string& layer_id, const QString& error);

    // Called when a map-build task starts / finishes for an imported layer.
    // "All Done" is withheld until all pending map loads have completed.
    void onMapLoadPending();
    void onMapLoadDone();

    void reanchor() {} // no-op; kept for API compatibility with old overlay

private slots:
    void onTick();

private:
    struct FileRow {
        std::string  layer_id;
        QString      display_name;
        QString      format;
        float        size_mb    = 0.f;

        QFrame*      card       = nullptr;
        QLabel*      badge      = nullptr;
        QLabel*      name_lbl   = nullptr;
        QLabel*      meta_lbl   = nullptr;
        QLabel*      status_lbl = nullptr;
        QProgressBar* bar       = nullptr;
        QLabel*      result_lbl = nullptr;

        enum class State { Active, Done, Failed } state = State::Active;
    };

    FileRow* findRow(const std::string& id);
    void     removeRowById(const std::string& id);
    void     clearFinishedRows();
    QFrame*  buildCard(FileRow& row, QWidget* parent);
    void     updateHeader();
    void     updateOverallProgress();
    void     checkAllDone();
    void     applyCardState(FileRow& row, FileRow::State s);

    QLabel*       m_title_lbl   = nullptr;
    QLabel*       m_sub_lbl     = nullptr;
    QProgressBar* m_overall_bar = nullptr;
    QWidget*      m_list_body   = nullptr;
    QVBoxLayout*  m_list_lay    = nullptr;
    QScrollArea*  m_scroll      = nullptr;
    QLabel*       m_elapsed_lbl = nullptr;
    QPushButton*  m_bg_btn      = nullptr;
    QPushButton*  m_close_btn   = nullptr;

    std::vector<FileRow> m_rows;
    int    m_queue_total        = 0;
    int    m_pending_map_loads  = 0;
    bool   m_all_done           = false;
    QTimer* m_timer      = nullptr;
    qint64  m_start_ms   = 0;
};

} // namespace dolphin::ui
