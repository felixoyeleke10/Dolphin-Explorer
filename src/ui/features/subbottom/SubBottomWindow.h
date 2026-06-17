#pragma once
#include "app/tasks/CancellationToken.h"
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"
#include "ui/features/subbottom/SubBottomViewStyle.h"
#include "ui/features/subbottom/SbpGainParams.h"
#include "ui/features/subbottom/SbpSignalParams.h"
#include "app/display/NavProcessingParams.h"
#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include "ui/shared/widgets/ViewerToolbar.h"
#include "ui/shell/ViewerWindow.h"
#include "core/SubBottomTrace.h"
#include <QWidget>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dolphin::ui { class AppState; }

class QLabel;
class QPoint;
class QProgressBar;
class QScrollBar;
class QTimer;
class QToolButton;

namespace dolphin::app {
class DataLayer;
class ImportService;
class OperationManager;
}

namespace dolphin::ui {

class SubBottomInspectorPanel;
class SubBottomView;

// -----------------------------------------------------------------------------
//  SubBottomWindow — top-level window for sub-bottom profiler (SBP) display.
//
//  Layout mirrors WaterfallWindow:
//    Toolbar                   (top)
//    SubBottomInspectorPanel   (left,  230 px) — metadata + scale controls
//    SubBottomView             (centre, expanding) — rendered seismic section
//    QScrollBar (horizontal)   (below view) — trace pan
//    SubBottomDisplayPanel     (right, 230 px) — palette/gain/bottom track
//    Status bar                (bottom)
//
//  Signal wiring handled in SubBottomWindow.cpp.
//  Data loading handled in SubBottomWindow.Load.cpp.
//  Toolbar + command palette in SubBottomWindow.Toolbar.cpp.
//  Status bar + cursor handlers in SubBottomWindow.Status.cpp.
// -----------------------------------------------------------------------------

class SubBottomWindow : public QWidget, public IViewerWindow {
    Q_OBJECT
public:
    explicit SubBottomWindow(AppState* app_state, QWidget* parent = nullptr);
    ~SubBottomWindow() override = default;

    void setLayer(app::DataLayer*      layer,
                  app::ImportService*  import_service,
                  const std::string&   source_path,
                  uint64_t             source_size_bytes = 0);
    void clearLayer();
    void reloadCurrentLayer();

    // IViewerWindow
    void onViewerRefresh(ViewerRefreshReason reason,
                         const std::string&  layer_id = {}) override;
    ViewerDataState viewerDataState() const override { return m_data_state; }

    const std::string& currentLayerId() const;

    void setProjectLayers(const std::vector<std::pair<std::string, std::string>>& layers);

    // Settings dialog accessors — read current panel/view state.
    SubBottomDisplayParams displayParams() const;
    int                    pxPerTrace()   const;
    float                  pxPerSample()  const;
    const SubBottomViewStyle& viewStyle() const;

    // Apply settings from SubBottomSettingsDialog.
    void applySettings(const SubBottomDisplayParams& params,
                       int px_per_trace, float px_per_sample,
                       const SubBottomViewStyle& style);

    // Apply display params only — called from the main-window right panel.
    void applyDisplayParams(const SubBottomDisplayParams& params);

    // Sync the palette index from the app-wide default without touching other params.
    void setPalette(int idx);

    // Update sound velocity (from AppState) without a disk reload.
    // Propagates to the display panel and depth readout.
    void setSoundVelocity(double sv);

    // Discard the current processed traces and re-run the pipeline from
    // m_traces_raw — no disk I/O.  No-op if no raw traces are held.
    void invalidateProcessedCache();

    // Inject the shared OperationManager (owns this window's load/process ops,
    // keyed so they supersede prior runs and respect the heavy-job cap). Set once,
    // after construction, by the owning coordinator.
    void setOperationManager(app::OperationManager* m) { m_op_mgr = m; }

    // Apply signal-processing params from the right-panel Gain / Signal modules.
    // Re-runs the processing pipeline on the in-memory raw trace copy; no disk I/O.
    void applyGainParams  (const SbpGainParams&   p);
    void applySignalParams(const SbpSignalParams&  p);

    // Apply navigation corrections (GPS smoothing / towfish layback / heading
    // offset) to this window's traces and re-process. The main-window coordinator
    // owns orchestration (per-layer storage + map-profile rebuilds); this only
    // refreshes the currently displayed line.
    void applyNavToLine(const dolphin::ui::NavProcessingParams& p);

protected:
    void closeEvent(QCloseEvent* e) override;

signals:
    void newFileRequested();
    void openFileRequested();
    void saveFileRequested();
    void metadataRequested();
    void settingsRequested();
    void prevLineRequested(const std::string& from_layer_id);
    void nextLineRequested(const std::string& from_layer_id);
    void cursorUpdated(float depth_m, double lat, double lon, bool is_projected);  // depth_m < 0 = cursor off-screen
    void layerChangeRequested(const std::string& id);
    void dataStateChanged(dolphin::ui::ViewerDataState state);

private slots:
    void onScrollbarMoved(int first_trace);
    void onViewScrollChanged(int first_trace, int total, int visible);
    void onCursorMoved(int trace_idx, float depth_s, double lat, double lon, bool nav_projected);
    void onCursorLeft();
    void onContextMenu(const QPoint& global_pos);
    void onProcDebounce();  // fires after proc debounce settles; launches the actual background task

private:
    // Cancels any in-flight task, then arms m_proc_debounce so rapid calls
    // (e.g. slider drags) collapse into a single pipeline run.
    void scheduleProcessing();

    void setDataState(ViewerDataState s) {
        if (m_data_state != s) { m_data_state = s; emit dataStateChanged(s); }
    }
    void buildToolbar();
    void buildStatusBar();
    QList<CommandPaletteItem> buildCommandItems();

    void startProgress();
    void finishProgress();

    // -- Widgets -----------------------------------------------------------
    SubBottomInspectorPanel* m_inspector           = nullptr;
    SubBottomView*           m_view                = nullptr;
    SubBottomDisplayPanel*   m_display             = nullptr;
    QScrollBar*              m_hscroll             = nullptr;
    ViewerToolbar*           m_toolbar             = nullptr;
    QToolButton*             m_btn_bottom_track_tb = nullptr;

    QLabel*        m_status_left   = nullptr;
    QLabel*        m_status_right  = nullptr;
    QProgressBar*  m_progress_bar  = nullptr;
    QTimer*        m_proc_debounce = nullptr;  // batches rapid processing triggers

    // -- Data -------------------------------------------------------------
    app::DataLayer*     m_layer             = nullptr;
    app::ImportService* m_import_service    = nullptr;
    std::string         m_source_path;
    uint64_t            m_source_size_bytes = 0;
    int                    m_total_traces = 0;
    app::OperationManager* m_op_mgr      = nullptr;  // owns load/process ops (keyed)
    ViewerDataState        m_data_state  = ViewerDataState::Idle;

    // Unprocessed traces — shared_ptr so scheduleProcessing() captures a pointer
    // instead of copying all data on the UI thread.  The background task copies
    // lazily once it actually starts, so cancelled tasks skip the copy entirely.
    std::shared_ptr<const std::vector<core::SubBottomTrace>> m_traces_raw;
    SbpGainParams                     m_gain_params;
    SbpSignalParams                   m_signal_params;
    NavProcessingParams               m_nav_params;   // display-time nav corrections

    AppState*           m_app_state         = nullptr;

    // -- Display state -----------------------------------------------------
    float               m_sound_half_speed  = 750.0f; // depth = time * this (= speed/2)
};

} // namespace dolphin::ui
