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
#include "core/Contact.h"
#include "core/SubBottomTrace.h"
#include <QPointF>
#include <QString>
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
class OperationManager;
}

namespace dolphin::ui {

class SubBottomInspectorPanel;
class SubBottomView;
class ContactPickingPanel;

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

    // Full project contact list, kept in sync by MainWindow via the event bus.
    // The window filters to the loaded line and shows marker diamonds on the
    // section (parity with the waterfall's contact overlay).
    void setProjectContacts(std::vector<core::Contact> contacts);

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
    // Restore per-layer state without rewriting global preferences.
    void restoreDisplayParams(const SubBottomDisplayParams& params);

    // Sync the palette index from the app-wide default without touching other params.
    void setPalette(int idx);

    // Enable/disable the inspector's Prev/Next Line buttons (ends of the line list).
    void setLineNavEnabled(bool has_prev, bool has_next);

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
    // User edited display params (palette/gain/contrast/invert/bottom-track)
    // INSIDE this window — forwarded from the display panel's user-action
    // signal only, never for programmatic pushes (applyDisplayParams/
    // restoreDisplayParams), so the authority round-trip cannot echo.
    void displayParamsEdited(dolphin::ui::SubBottomDisplayParams params);
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
    // Contact placed at a trace (geo from the trace nav; depth_m from the clicked
    // travel time × half sound-speed). MainWindow creates the core::Contact.
    void contactCreated(double lat, double lon, bool is_projected, float depth_m,
                        const QString& classification, const QString& line_id,
                        uint64_t abs_trace);
    // Polygon/polyline feature drawn on the section; vertices are (lon,lat).
    void featureCreated(const std::vector<QPointF>& lonlat_vertices, bool polygon,
                        bool is_projected, const QString& classification,
                        const QString& line_id);
    // "Clear All Contacts" pressed in the Contact Picking section.
    void clearAllContactsRequested();
    // Open the shared "Edit contact details" editor. id = specific contact
    // (marker double-click) or 0 (panel button → first contact on this line);
    // line_id scopes the editor's Prev/Next to this line's contacts.
    void contactEditRequested(uint64_t id, const QString& line_id);

private slots:
    void onScrollbarMoved(int first_trace);
    void onViewScrollChanged(int first_trace, int total, int visible);
    void onCursorMoved(int trace_idx, float depth_s, double lat, double lon, bool nav_projected);
    void onCursorLeft();
    void onContextMenu(const QPoint& global_pos, uint64_t contact_id);
    void onProcDebounce();  // fires after proc debounce settles; launches the actual background task

private:
    // Cancels any in-flight task, then arms m_proc_debounce so rapid calls
    // (e.g. slider drags) collapse into a single pipeline run.
    void scheduleProcessing();

    // Converts m_project_contacts for the loaded line into view ContactMarks.
    void refreshContactOverlay();
    // Reflect the active feature tool on the toolbar toggles (signal-blocked).
    void syncFeatureToolButtons(int tool);

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
    ContactPickingPanel*     m_contact_panel       = nullptr;
    // Feature drawing toolbar toggles (1=polygon, 2=line, 3=pen)
    QToolButton*             m_btn_feat_poly       = nullptr;
    QToolButton*             m_btn_feat_line       = nullptr;
    QToolButton*             m_btn_feat_pen        = nullptr;

    QLabel*        m_status_left   = nullptr;
    QLabel*        m_status_right  = nullptr;
    QProgressBar*  m_progress_bar  = nullptr;
    QTimer*        m_proc_debounce = nullptr;  // batches rapid processing triggers

    // -- Data -------------------------------------------------------------
    std::vector<core::Contact> m_project_contacts;  // full list, synced via the bus
    app::DataLayer*     m_layer             = nullptr;
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

    // Prev/Next availability (set by the coordinator) — gates the inspector buttons,
    // command-palette entries, and context-menu actions consistently.
    bool m_has_prev_line = false;
    bool m_has_next_line = false;

    // -- Display state -----------------------------------------------------
    float               m_sound_half_speed  = 750.0f; // depth = time * this (= speed/2)
};

} // namespace dolphin::ui
