#pragma once
#include <QDialog>
#include <QString>
#include <string>
#include <vector>

class QFrame;
class QCloseEvent;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTimer;
class QHBoxLayout;
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
    // Like updateJob, but sets an explicit status line (e.g. "Applying corrections… 70%")
    // instead of the default "Reading N%" — lets a processing op describe its phase.
    void updateJob(const std::string& layer_id, int percent, const QString& status);

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

    // Drop all batch/map-phase state and hide. Call when the project changes: the
    // previous project's in-flight builds are cancelled, so its counters/cards must
    // not carry into the next project (which would leave the panel stale or stuck).
    // Bumps a generation so a late onMapLoadDone from a cancelled build is ignored.
    void resetState();

    // Embed this panel as a child overlay of `host` (instead of a top-level window).
    // A top-level popup over the frameless main window makes it blink when shown
    // during project open; an in-window child overlay cannot. Anchored bottom-centre
    // over the host and repositioned on host resize.
    void embedIn(QWidget* host);

    void reanchor() {} // no-op; kept for API compatibility with old overlay

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;  // reposition on host resize

private slots:
    void onTick();

private:
    struct FileRow {
        std::string  layer_id;
        QString      display_name;
        QString      format;
        float        size_mb    = 0.f;
        int          percent    = 0;   // reading progress (no per-row bar widget now)

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
    void     buildStageChips(int n);   // (re)create the pipeline stage chips
    void     updateStages();           // refresh stage chips + "now" line from state
    void     runInBackground();
    void     showForActiveBatch();
    void     positionInParent();   // anchor bottom-centre within the embed host

    QLabel*       m_title_lbl   = nullptr;
    QLabel*       m_sub_lbl     = nullptr;
    QProgressBar* m_overall_bar = nullptr;
    QWidget*      m_list_body   = nullptr;
    QVBoxLayout*  m_list_lay    = nullptr;
    QScrollArea*  m_scroll      = nullptr;
    QLabel*       m_elapsed_lbl = nullptr;
    QPushButton*  m_bg_btn      = nullptr;
    QPushButton*  m_close_btn   = nullptr;

    // Stage pipeline — the primary view (the per-line card list is kept for state
    // but hidden). Chips are built once per batch from the operation kind.
    QWidget*             m_stage_box        = nullptr;
    QHBoxLayout*         m_stage_lay        = nullptr;
    std::vector<QLabel*> m_stage_lbls;
    bool                 m_stages_built     = false;
    bool                 m_op_is_processing = false;  // true = no map phase (bake/process)
    bool                 m_has_map_phase    = false;
    int                  m_map_total        = 0;

    std::vector<FileRow> m_rows;
    int    m_queue_total        = 0;
    int    m_pending_map_loads  = 0;
    // Map-load "done" events still expected from builds cancelled by a project change.
    // They fire late (the cancelled worker finishes shortly after) and must be absorbed
    // so they don't decrement the NEW project's pending count.
    int    m_stale_done_expected = 0;
    bool   m_all_done           = false;
    bool   m_backgrounded       = false;
    bool   m_embedded           = false;   // child overlay (no top-level window)
    QWidget* m_host             = nullptr; // embed host (viewport area)
    QTimer* m_timer      = nullptr;
    qint64  m_start_ms   = 0;
};

} // namespace dolphin::ui
