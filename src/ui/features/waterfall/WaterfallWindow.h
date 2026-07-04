#pragma once
#include "app/tasks/CancellationToken.h"
#include "app/display/NavProcessingParams.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/waterfall/WaterfallSettingsDialog.h"
#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include "ui/shared/widgets/ViewerToolbar.h"
#include "ui/shell/ViewerWindow.h"
#include "core/Contact.h"
#include "core/SidescanPing.h"
#include <QCloseEvent>
#include <QPointF>
#include <QWidget>
#include <string>
#include <utility>
#include <vector>

namespace dolphin::ui { class AppState; }

class QComboBox;
class QLabel;
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

class WaterfallView;
class WaterfallInspectorPanel;
class WaterfallAnalysisPanel;
class WaterfallQcStrip;

// -----------------------------------------------------------------------------
//  WaterfallWindow — top-level window for sidescan waterfall display.
//
//  Assembles the waterfall subsystem:
//    WaterfallInspectorPanel  (left)   — survey metadata + nav buttons
//    WaterfallView            (centre) — rendered waterfall image
//    QScrollBar               (centre-right) — vertical survey scroll
//    WaterfallAnalysisPanel   (right)  — image processing sliders
//
//  Also owns the toolbar, bottom status bar, and async data loading.
//  Panel-building and metadata-refresh logic have moved into the panel classes.
// -----------------------------------------------------------------------------

class WaterfallWindow : public QWidget, public IViewerWindow {
    Q_OBJECT
public:
    explicit WaterfallWindow(AppState* app_state, QWidget* parent = nullptr);
    ~WaterfallWindow() override;

    void setLayer(app::DataLayer*      layer,
                  app::ImportService*  import_service,
                  const std::string&   source_path,
                  uint64_t             source_size_bytes = 0);
    void clearLayer();

    // IViewerWindow
    void onViewerRefresh(ViewerRefreshReason reason,
                         const std::string&  layer_id = {}) override;
    ViewerDataState viewerDataState() const override { return m_data_state; }

    // Enable or disable the GPU (OpenGL) rendering path for the waterfall canvas.
    // Delegates to WaterfallView::setGpuAccel — see that method for semantics.
    void setGpuAccel(bool enabled);

    // Enable/disable the inspector's Prev/Next Line buttons (ends of the line list).
    void setLineNavEnabled(bool has_prev, bool has_next);

signals:
    void newFileRequested();
    void openFileRequested();
    void saveFileRequested();
    void propertiesRequested();
    void metadataRequested();
    void settingsRequested();
    // Carry the layer ID currently shown in the waterfall so MainWindow can
    // navigate relative to it, not relative to whatever m_active_layer_id
    // happens to be (the two can diverge when the user opens the waterfall
    // and then clicks Prev/Next without first touching the layer picker).
    void prevLineRequested(const std::string& from_layer_id);
    void nextLineRequested(const std::string& from_layer_id);

    // -- State-reflection signals — received by MainWindow -----------------
    // Fired on every cursor move; MainWindow mirrors range/position to its
    // own status bar so the user sees the same info without switching windows.
    void cursorUpdated(float range_m, const QString& side,
                       double lat, double lon, bool is_projected);

    // Fired when the user places a point contact pick.  MainWindow creates the
    // core::Contact in the project; consumers refresh from the project signals.
    // abs_row: absolute ping row in the full survey (window_first_row + local row).
    // channel_idx: 0 = Port, 1 = Starboard.
    void contactCreated(float range_m, double lat, double lon, bool is_projected,
                        const QString& classification,
                        const QString& line_id,
                        uint64_t abs_row,
                        int channel_idx,
                        const QPixmap& snapshot);

    // Open the shared "Edit contact details" editor. id = specific contact
    // (marker double-click) or 0 (panel button → first contact on this line);
    // line_id scopes the editor's Prev/Next to this line's contacts.
    void contactEditRequested(uint64_t id, const QString& line_id);

    // Fired when the user draws a polygon/polyline feature on the waterfall.
    // vertices are (lon,lat); polygon=true closes the shape. MainWindow creates the
    // core::Feature in the project.
    void featureCreated(const std::vector<QPointF>& lonlat_vertices, bool polygon,
                        bool is_projected, const QString& classification,
                        const QString& line_id);
    // "Clear All Contacts" pressed — clears project contacts (same as SBP), not just
    // the local overlay, so the action means the same thing in both viewers.
    void clearAllContactsRequested();

    // Fired when the user clicks the Contact Manager button in the toolbar.
    void contactManagerRequested();

    // Fired whenever display params are applied (palette, SRC, gain, …).
    // MainWindow uses this to mark the project as having unsaved changes.
    void paramsApplied();

    // Fired when the fraction of viewed pings changes by ≥ 0.5 %.
    // Carries the layer ID and the new fraction in [0, 1].
    // MainWindow can write this back to DataLayer::qc_viewed_fraction.
    void qcViewedFractionChanged(const std::string& layer_id, float fraction);

    // Fired when the palette changes — MainWindow syncs the Properties inspector.
    void paletteChanged(int idx);

    // Fired when "Apply to All Lines" is clicked.
    // MainWindow can use this to propagate params across all project layers.
    void applyToAllRequested();

    // Fired whenever the viewer's data lifecycle state changes.
    // Allows MainWindow to drive a global loading indicator or status label.
    void dataStateChanged(dolphin::ui::ViewerDataState state);

    // Fired when the user runs nav processing on all lines.
    // MainWindow can propagate the correction to other loaded layers.
    void navProcessAllLinesRequested(dolphin::ui::NavProcessingParams params);

    // Fired when the user clicks "Set CRS" in the inspector panel.
    // Carries the layer ID currently shown so MainWindow picks the right layer
    // even when the waterfall has navigated away from m_active_layer_id.
    void setCrsRequested(const std::string& from_layer_id);

    // Fired when the user clicks a file in the FILES list.
    // MainWindow should call onLayerSelected(id) in response.
    void layerChangeRequested(const std::string& id);

public:
    // External palette sync — updates inspector combo without re-emitting, then applies.
    void setPalette(int idx);

    // External channel sync — updates m_display_channel and applies immediately.
    void            setDisplayChannel(DisplayChannel ch);
    DisplayChannel  displayChannel() const { return m_display_channel; }

    // Push a fresh copy of all project contacts to the waterfall overlay.
    // MainWindow calls this whenever contacts are added/removed from the project.
    void setProjectContacts(std::vector<core::Contact> contacts);

    // Read the current display params (e.g. for syncing external control panels).
    const WaterfallParams& currentParams() const;

    // Returns the ID of the layer currently loaded in the waterfall, or empty.
    const std::string& currentLayerId() const;

    // Returns a copy of the raw pings held by the view, including any bottom_pick
    // fields set by seabed detection or user editing.  Empty if no data is loaded.
    std::vector<core::SidescanPing> currentRawPings() const;

    void reloadCurrentLayer();

    // Returns the fraction of this layer's pings that have been scrolled past.
    float qcFraction() const { return m_qc_fraction; }

    // Populate the FILES list with all sidescan layers in the project.
    void setProjectLayers(const std::vector<std::pair<std::string, std::string>>& layers);
    // Highlight the active file in the FILES list (does not emit layerChangeRequested).
    void setActiveLine(const std::string& id);

    // Settings dialog accessors.
    WaterfallSettingsDialog::Settings wfSettings() const;
    void applyWfSettings(const WaterfallSettingsDialog::Settings& s);

    // Discard the current processed rows and re-run the full pipeline from the
    // view's held raw pings — no disk I/O.  No-op if no pings are loaded.
    void invalidateProcessedCache();

    // Inject the shared OperationManager (owns this window's pipeline ops, keyed
    // "wf:pipeline" so load/repipe/nav-process supersede each other and respect
    // the heavy-job cap). Set once, after construction, by the coordinator.
    void setOperationManager(app::OperationManager* m) { m_op_mgr = m; }

public slots:
    // Apply params from an external control panel (GainControlPanel, ImagingControlPanel).
    // Triggers a visual flash and emits paramsApplied().
    void applyExternalParams(const WaterfallParams& p);
    // Same as applyExternalParams but also emits applyToAllRequested so
    // MainWindow can propagate the params to every project layer.
    void applyExternalParamsToAll(const WaterfallParams& p);

    // Live nav correction for the line shown in this window. The main-window
    // Nav/Geometry panels are wired to model-owned MainWindow slots at
    // construction; this is the viewer-refresh entry point those slots call while
    // the window is open. (Apply-to-all persistence is handled by MainWindow via
    // navProcessAllLinesRequested, emitted by this window's analysis panel.)
    void applyNavToLine(const dolphin::ui::NavProcessingParams& p);

protected:
    void closeEvent(QCloseEvent* ev) override;

private slots:
    void onContextMenu(QPoint globalPos);
    void onPrevFix();
    void onNextFix();
    void onFrequencyBandChanged(int index);
    void onModeChanged(int mode);
    void onCursorMoved(int ping_idx, float range_port_m, float range_stbd_m,
                       float depth_port_m, float depth_stbd_m,
                       double lat, double lon, bool is_projected);
    void onScrollBeyondBounds(int direction);
    void onViewScrollChanged(int scroll_row, int total_rows, int visible_rows);
    void onScrollbarMoved(int abs_row);
    void onScrollDebounce();
    void onRepipeDebounce();  // fires after repipe debounce settles; launches the actual pipeline

private:
    enum Mode { ModeNavigate = 0, ModeFix, ModeReview, ModeAnalyze };
    void setDataState(ViewerDataState s) {
        if (m_data_state != s) { m_data_state = s; emit dataStateChanged(s); }
    }
    void buildToolbar();
    void buildBottomBar();
    QList<CommandPaletteItem> buildCommandItems();

    void pushParams();
    void refreshInspector();
    void loadWindow(int abs_row);

    // Progress bar helpers — call these instead of touching m_progress_bar directly.
    void startProgress();   // begin smooth 0→90% fill (async load started)
    void finishProgress();  // snap to 100% then hide  (async load finished)
    void flashProgress();   // instant 100% flash then hide (synchronous apply done)

    // Deactivates the contact pick tool on all three surfaces (view, panel button,
    // toolbar button) and clears the window-local contact overlay.
    void resetContactTool();

    // Converts m_project_contacts for the current layer + window to WfContacts
    // and pushes them to m_view as the external overlay.
    void refreshContactOverlay();
    // Reflect the active feature tool on the toolbar toggles (signal-blocked).
    void syncFeatureToolButtons(int tool);

    // Apply nav corrections then re-pipeline entirely off the UI thread.
    // Replaces m_raw_pings with the corrected set on completion.
    void scheduleNavProcessing(const NavProcessingParams& p);

    // Returns the estimated total row count for the whole survey.
    // Uses the actual entries_per_row ratio if one has been measured;
    // falls back to sidescanCount() (1 row = 1 entry) so single-channel
    // files never get cut off at half their length.
    int estimatedTotalRows() const;

    // Mark [abs_first, abs_last) as viewed; merges into m_viewed_ranges and
    // emits qcViewedFractionChanged if the fraction changed by ≥ 0.5 %.
    void markRowsAsViewed(int abs_first, int abs_last);

    // QSettings key base for the current layer's QC data.
    QString qcSettingsKey() const;

    // Persist m_viewed_ranges to QSettings for the current layer.
    // Called before the layer pointer is replaced (setLayer) or cleared.
    void saveQcRanges();

    // Restore m_viewed_ranges from QSettings for layer and update fraction.
    void loadQcRanges();

    // -- Widgets -----------------------------------------------------------
    WaterfallView*            m_view      = nullptr;
    WaterfallInspectorPanel*  m_inspector = nullptr;
    WaterfallAnalysisPanel*   m_analysis  = nullptr;
    QScrollBar*               m_vscroll   = nullptr;
    WaterfallQcStrip*         m_qc_strip  = nullptr;
    ViewerToolbar*            m_toolbar   = nullptr;
    QTimer*                   m_scroll_debounce  = nullptr;
    QTimer*                   m_repipe_debounce  = nullptr;  // batches rapid repipe triggers
    QToolButton*              m_btn_contact  = nullptr;  // "Add Contact" toolbar toggle
    // Feature drawing toolbar toggles (1=polygon, 2=line, 3=pen)
    QToolButton*              m_btn_feat_poly = nullptr;
    QToolButton*              m_btn_feat_line = nullptr;
    QToolButton*              m_btn_feat_pen  = nullptr;
    QComboBox*                m_freq_selector = nullptr; // frequency band picker (dual-freq only)

    QLabel*       m_status_left   = nullptr;   // errors / mode messages (incl. loading)
    QLabel*       m_status_centre = nullptr;
    QLabel*       m_status_right  = nullptr;
    QProgressBar* m_progress_bar   = nullptr;   // bottom-left activity indicator
    QTimer*       m_progress_delay  = nullptr;  // delays spinner show so fast ops don't flash it

    // -- Data -------------------------------------------------------------
    std::vector<core::Contact> m_project_contacts;  // full project contact list, kept in sync

    app::DataLayer*     m_layer             = nullptr;
    app::ImportService* m_import_service    = nullptr;
    std::string         m_source_path;
    uint64_t            m_source_size_bytes = 0;
    int                 m_active_mode       = ModeNavigate;

    app::OperationManager*        m_op_mgr      = nullptr;  // owns pipeline ops (keyed)
    ViewerDataState               m_data_state  = ViewerDataState::Idle;
    // Prev/Next availability (set by the coordinator) — gates the inspector buttons,
    // command-palette entries, and context-menu actions consistently.
    bool m_has_prev_line = false;
    bool m_has_next_line = false;
    int   m_total_ssc_entries     = 0;
    int   m_window_first_row      = 0;
    float m_entries_per_row       = 1.0f;
    float m_selected_frequency_hz = 0.f;   // 0 = no filter; >0 = show only this band
    bool  m_reset_view_next       = true;
    int   m_pending_abs_row       = -1;    // row buffered by scroll debounce
    bool  m_scrollbar_dragging    = false; // true while the user holds the scroll thumb

    AppState*      m_app_state       = nullptr;

    // Loaded from QSettings on construction; settable from WaterfallSettingsDialog.
    int            m_window_size     = 4000;
    DisplayChannel m_display_channel = DisplayChannel::Both;
    bool           m_show_amp_bar    = true;

    // -- QC viewed-range tracking ------------------------------------------
    // Sorted, non-overlapping [first, last) absolute row ranges the user has
    // scrolled past in the current session.  Persisted to QSettings per layer.
    std::vector<std::pair<int,int>> m_viewed_ranges;
    float                           m_qc_fraction = 0.f;
};

} // namespace dolphin::ui
