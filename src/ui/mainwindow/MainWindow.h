#pragma once
#include <QDateTime>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include "app/project/Project.h"
#include "core/SpatialRef.h"
#include "ui/bottom/DiagnosticsHub.h"
#include "ui/features/map/MapTypes.h"
#include "ui/features/waterfall/NavProcessingParams.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/subbottom/SbpGainParams.h"
#include "ui/features/subbottom/SbpSignalParams.h"
#include "app/tasks/OperationManager.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/systems/AppState.h"
#include "ui/systems/WindowRegistry.h"
#include "ui/systems/ProjectEventBus.h"

class QAction;
class QDialog;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPropertyAnimation;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QToolBar;
class QToolButton;
class QUndoStack;

namespace dolphin::app {
class DataLayer;
class ImportJobManager;
class ImportService;
class ProcessingService;
class SidescanCorrectionService;
class SubBottomCorrectionService;
}

namespace dolphin::ui {

struct ImportDialogResult;

class BottomDockPanel;
class ExecutionController;
class ExecutionProgressDialog;
class GeodesyPanel;
class InspectorPanel;
class LayerPickerWidget;
class LineListPanel;
class MapView;
class MapViewportHost;
class NodeGraphWindow;
class ProcessingController;
class SidescanViewController;
class SubBottomWindow;
class WaterfallWindow;

class ConversationPanel;
class CommandBar;
class PanelChatWidget;
class DataLibraryWindow;
class GainControlPanel;
class HeadingInfoPanel;
class ImagingControlPanel;
class MainStatusBar;
class NavInfoPanel;
class ProcessingDialog;
class ProcessingWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool nativeEvent(const QByteArray& event_type, void* message,
                     qintptr* result) override;

private slots:
    // Project
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProjectFolder();
    void onCloseProject();
    void onImportFile();
    void onGeodeticSettings();

    // Export stubs
    void onExportCsv();
    void onExportGeotiff();
    void onExportKmz();
    void onExportNav();
    void onExportPdf();
    void onExportScreenshot();

    // Processing
    void onRunAllLayers();
    void onRunSelectedLayer();
    void onOpenProcessingWindow();

    // Tool stubs
    void onToolCursor();
    void onToolSelect();
    void onToggle3D();
    void onToolZoom();
    void onToolMeasure();
    void onNavEditor();
    void onMeasurementUpdated(double metres);
    void onContactPickedOnMap(double lon, double lat);
    void onWaterfallOpen();
    void onSubBottomOpen();
    void onBottomTrack();
    void onWaterfallPrevLine(const std::string& from_layer_id);
    void onWaterfallNextLine(const std::string& from_layer_id);
    void onWaterfallMetadata();
    void onWaterfallSettings();

    // Waterfall → MainWindow state reflection
    void onWaterfallCursorUpdated(float range_m, const QString& side,
                                  double lat, double lon, bool is_projected);
    void onWaterfallContactCreated(float range_m, double lat, double lon,
                                   bool is_projected,
                                   const QString& classification,
                                   const QString& line_id,
                                   uint64_t abs_row,
                                   int channel_idx);
    void onWaterfallParamsApplied();
    void onWaterfallSetCrs(const std::string& from_layer_id);
    void onWaterfallNavProcessAllLines(dolphin::ui::NavProcessingParams params);

    // Contact / layer stubs
    void onAddContact();
    void onLineProps();
    void onResetRaw();
    void onRenumberContacts();
    void onClearContacts();

    // Map sonar preview quality
    void onMapSonarQuality(MapSonarQuality q);

    // Map context menu
    void onMapContextMenu(QPoint globalPos);
    void onNavigateBack();
    void onNavigateForward();

    // Activity bar — switch context panel
    void onActivityPanel(int panel_id);
    void togglePanel(int panel_id, bool force_open = false);
    void toggleProperties();

    // Viewport — switch main view
    void onViewTabChanged(int index);

    // Data events
    void onLayerSelected(const std::string& layer_id);
    void onRemoveLayer(const std::string& layer_id);
    void onRemoveLayers(const std::vector<std::string>& layer_ids);
    void onRenameLayer(const std::string& layer_id);
    void onRunLayers(const std::vector<std::string>& layer_ids);
    void onExportLayers(const std::vector<std::string>& layer_ids, const QString& format);
    void onMergeLayers(const std::vector<std::string>& layer_ids);
    void onRemoveContact(uint64_t contact_id);
    void onRevealSource(const std::string& source_id);
    void updateControlsForModality(const app::DataLayer* layer);
    void onContactSelected(uint64_t contact_id);
    void onContactPicked(double lat, double lon, uint64_t artifact_id, uint32_t sample_idx);
    void onAbout();
    void onLayerVisibilityChanged(const std::string& layer_id, bool visible);
    void onPaletteChanged(int idx);
    void refreshLoadingIndicator();
    void onToggleContextPanel();
    void onTogglePropertiesPanel();
    void onPropsTabChanged(int tab);
    void toggleBottomPanel();
    void onDataLibraryOpen();
    void onNodeGraph();
    void onAppSettings();
    void onAutoSave();
    void applyLiveSettings(const AppSettingsDialog::Settings& s);

    // Apply stored per-layer nav corrections to the waterfall after setLayer.
    void applyStoredNavParams(const std::string& layer_id);

    // SSS map diagnostics — extracted from MainWindow.cpp constructor lambda
    void onMapDiagnosticsReady(const QString& layer_id, const NavStats& stats);

private:
    // Legacy panel ids. The left dock is now fixed to File Explorer, while
    // PanelWaterfall is still used as a command target for opening the
    // waterfall window.
    enum ContextPanel {
        PanelExplorer = 0,
        PanelWaterfall,    // opens/focuses the waterfall window
        PanelDataLibrary,
        PanelProcessing,
        PanelContacts,
        PanelAnalyze,
        PanelAI,
        PanelPresent,
        PanelReport,
        PanelSettings,
        PanelGeodesy,      // left context panel — CRS / coordinate management
        PanelCount
    };

    void setupToolBar();
    void setupMenuBar();
    void setupStatusBar();
    void setupCentralWidget();
    void setupTitleBar();

    // setupMenuBar sub-builders
    void buildFileMenu();
    void buildEditMenu();
    void buildProjectMenu();
    void buildViewMenu();
    void buildProcessingMenu();
    void buildNodeMenu();

    // setupCentralWidget sub-builders
    QFrame*  buildActivityBar(QWidget* parent);
    void     buildContextPanel(QWidget* parent);
    QWidget* buildMainArea(QWidget* parent);
    void     buildPropertiesPanel(QWidget* parent);
    QFrame*  buildRightToolBar(QWidget* parent);

    void loadProject(const std::string& path);
    void addToRecentProjects(const QString& path);
    void rebuildRecentMenu();
    void bindProjectUi();
    void showImportDialog(const QStringList& paths,
                          const std::vector<core::ArtifactType>& module_filter = {});
    bool ensureProjectForImport(const ImportDialogResult& res);
    QList<CommandPaletteItem> buildCommandItems();
    void setWindowTitleFromProject();
    void applyWorkspaceState(int panel_id, bool props_open, bool toolbar_visible);
    void setPropertiesOpen(bool open);
    void setRightToolBarVisible(bool visible);
    bool rightToolBarVisible() const;
    bool panelUsesContextStack(int panel_id) const;
    int  normalizePanelId(int panel_id) const;
    int  rightDockWidth() const;
    void updateContextInfo();
    void appendJobMessage(const QString& message);
    void updateActionStates();
    void clearNavigationHistory();
    void pruneNavigationHistory();
    void recordNavigationSelection(const std::string& layer_id);
    void updateNavigationButtons();
    void refreshInspectorModalities();

    QWidget* makeContextPlaceholder(const QString& title, const QString& body);
    void     refreshSidebarSections(const QStringList& paths);

    // Processing dialog — shared across all long-running operations.
    void taskBegin(const QString& id, const QString& label);
    void taskDone (const QString& id);
    void taskFail (const QString& id, const QString& error = {});
    void onCancelProcessing();

    ProcessingDialog* m_processing_dlg = nullptr;

    DiagnosticsHub*   m_diag_hub     = nullptr;
    BottomDockPanel*  m_bottom_panel = nullptr;

    std::shared_ptr<app::Project> m_project;
    std::string                   m_active_layer_id;
    bool                          m_project_dirty    = false;
    uint64_t                      m_project_load_gen = 0;   // incremented on every load/close; guards deferred timer
    bool                          m_save_in_progress = false; // suppresses autosave while a save dialog is open
    core::SpatialRef              m_pending_crs;   // set before any project is open; pre-fills ImportDialog  // unsaved changes indicator

    app::OperationManager*  m_op_mgr = nullptr;  // central job runner + cancel registry
    app::ImportService*     m_import_service     = nullptr;
    app::ImportJobManager*  m_import_job_mgr     = nullptr;
    app::ProcessingService* m_processing_service = nullptr;

    // Maps import layer_id → DiagnosticsHub job_id for active indexing jobs.
    std::unordered_map<std::string, uint32_t> m_import_job_ids;
    // Maps OperationManager op_id → DiagnosticsHub job_id for bridge.
    std::unordered_map<uint32_t, uint32_t>    m_op_job_ids;

    SidescanViewController*            m_sss_ctrl        = nullptr;
    ExecutionController*               m_import_ctrl     = nullptr;
    ProcessingController*              m_proc_ctrl       = nullptr;
    app::SidescanCorrectionService*    m_correction_svc      = nullptr;
    app::SubBottomCorrectionService*   m_sbp_correction_svc  = nullptr;

    // Map sonar preview quality actions (index == MapSonarQuality int value)
    std::array<QAction*, 6> m_act_map_quality{};

    // Undo / redo
    QUndoStack* m_undo_stack  = nullptr;
    QAction*    m_act_undo    = nullptr;
    QAction*    m_act_redo    = nullptr;

    // Shared toolbar actions
    QAction* m_act_save        = nullptr;
    QAction* m_act_open_folder = nullptr;
    QAction* m_act_run_layer = nullptr;
    QAction* m_act_run_all   = nullptr;
    QToolButton* m_export_btn          = nullptr;
    QToolButton* m_cursor_btn          = nullptr;
    QToolButton* m_select_btn          = nullptr;
    QToolButton* m_zoom_btn            = nullptr;
    QToolButton* m_measure_btn         = nullptr;
    QToolButton* m_contact_btn         = nullptr;
    QToolButton* m_settings_btn        = nullptr;
    QToolButton* m_btn_sbp_open        = nullptr;
    std::vector<QAction*> m_export_actions;

    // Activity bar button map: panel_id → button (for check state management)
    QMap<int, QToolButton*> m_activity_btns;

    // Layout: fixed file explorer dock + main area + right tool bar
    QStackedWidget* m_context_stack    = nullptr;
    QWidget*        m_left_edge_strip  = nullptr;
    QWidget*        m_right_edge_strip = nullptr;
    QFrame*         m_props_panel      = nullptr;
    QFrame*         m_right_tool_bar   = nullptr;
    int                 m_active_panel   = 0;
    bool                m_panel_open     = false;
    QPropertyAnimation* m_panel_anim     = nullptr;

    // Properties panel (right-side dock)
    bool                m_props_open      = true;
    bool                m_props_collapsed = false;
    int                 m_props_width     = 260;       // user-resizable
    QLabel*             m_props_title         = nullptr;
    QToolButton*        m_props_collapse_btn  = nullptr;
    QToolButton*        m_props_tab_tools     = nullptr;
    QToolButton*        m_props_tab_chats     = nullptr;
    QToolButton*        m_props_tab_history   = nullptr;
    QStackedWidget*     m_props_stack         = nullptr;
    QListWidget*        m_props_history_list  = nullptr;
    QPropertyAnimation* m_props_anim          = nullptr;

    enum class ActivityKind {
        Import        = 0,
        Processing    = 1,
        Palette       = 2,
        DisplayParams = 3,
        NavCorrection = 4,
        Visibility    = 5,
        CrsChange     = 6,
        TagChange     = 7,
        GroupChange   = 8,
        Export        = 9,
        ContactPick   = 10,
    };
    struct ProjectActivityEntry { ActivityKind kind; QString description; QDateTime timestamp; };
    std::vector<ProjectActivityEntry> m_activity_log;
    void rebuildHistoryList();
    void recordActivity(ActivityKind kind, const QString& description);

    // Legacy floating layer picker. The main shell now uses the fixed File
    // Explorer dock instead, so this typically remains null.
    LayerPickerWidget*      m_layer_picker   = nullptr;

    // Import progress overlay (bottom-centre of viewport)
    ExecutionProgressDialog* m_import_overlay = nullptr;

    // Inspector (lives in Properties overlay)
    InspectorPanel*    m_inspector     = nullptr;

    // Panel pointers sourced from RightPanelHost — used for waterfall signal wiring.
    NavInfoPanel*        m_nav_panel     = nullptr;
    HeadingInfoPanel*    m_heading_panel = nullptr;
    GainControlPanel*    m_gain_panel    = nullptr;
    ImagingControlPanel* m_imaging_panel = nullptr;

    QScrollArea*        m_props_scroll    = nullptr;
    PanelChatWidget*    m_chat_widget     = nullptr;

    // Fixed left dock — project / files / layers / contacts tree
    LineListPanel*    m_line_list                = nullptr;
    QLabel*           m_context_title            = nullptr;
    QToolButton*      m_context_collapse_btn     = nullptr;
    bool              m_context_collapsed        = false;
    QListWidget*      m_sidebar_recent_list      = nullptr;

    // Geodesy standalone window (lazy-created on first open)
    QDialog*          m_geodesy_win    = nullptr;
    GeodesyPanel*     m_geodesy_panel  = nullptr;

    // Core systems — single instances owned by MainWindow.
    AppState*            m_app_state        = nullptr;
    WindowRegistry*      m_window_registry  = nullptr;
    ProjectEventBus*     m_event_bus        = nullptr;
    // (task tracking absorbed by m_op_mgr — see OperationManager)

    // Per-layer nav corrections applied by "Apply to All Lines".
    // Keyed by layer ID; looked up whenever the waterfall opens a layer.
    std::unordered_map<std::string, NavProcessingParams> m_layer_nav_params;

    // Per-layer display params moved to DataLayer::sss_display_state /
    // sbp_display_state — no separate MainWindow maps needed.

    // Layer IDs with an in-flight SubBottom profile build. Guards onLayerSelected
    // against spawning a second QFutureWatcher for the same layer.
    std::unordered_set<std::string> m_pending_sbp_builds;

    // App settings dialog (lazy-created on first open)
    QDialog*          m_app_settings_win = nullptr;
    QTimer*           m_autosave_timer   = nullptr;

    // Recent Projects submenu (owned by the File menu)
    QMenu*            m_recent_menu      = nullptr;

    // Viewport widgets
    MapViewportHost*   m_viewport_host   = nullptr;   // owns 2D + 3D views
    MapView*           m_map_view        = nullptr;   // = m_viewport_host->view2D()
    WaterfallWindow*   m_waterfall_win   = nullptr;
    SubBottomWindow*   m_sbp_win         = nullptr;
    QPointer<QWidget>  m_sbp_metadata_win;
    NodeGraphWindow*   m_node_graph_win  = nullptr;
    ProcessingWindow*  m_processing_win  = nullptr;
    QPointer<QWidget>  m_metadata_win;               // MetadataWindow (lazy-created)

    // Conversation panel (floating overlay below the unified bar)
    ConversationPanel*    m_conv_panel    = nullptr;

    // Data Library — lazy-created when PanelDataLibrary is first opened.
    DataLibraryWindow* m_data_library_win = nullptr;

    // Title bar centre widget
    CommandBar*  m_cmd_bar  = nullptr;

    // Title bar layout toggle buttons (corner widget)
    QPushButton* m_btn_nav_back          = nullptr;
    QPushButton* m_btn_nav_forward       = nullptr;
    QPushButton* m_btn_primary_sidebar   = nullptr;
    QPushButton* m_btn_bottom_panel      = nullptr;
    QPushButton* m_btn_secondary_sidebar = nullptr;
    std::vector<std::string> m_navigation_history;
    int                      m_navigation_index = -1;
    bool                     m_replaying_navigation = false;

    // Status bar
    MainStatusBar* m_status_bar = nullptr;
};

} // namespace dolphin::ui

using MainWindow = dolphin::ui::MainWindow;
