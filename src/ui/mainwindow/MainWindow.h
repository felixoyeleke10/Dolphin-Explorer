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
#include "app/display/NavProcessingParams.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/subbottom/SbpGainParams.h"
#include "ui/features/subbottom/SbpSignalParams.h"
#include "app/tasks/OperationManager.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/mainwindow/ProjectSessionController.h"
#include "ui/mainwindow/LayerDisplayCoordinator.h"
#include "ui/mainwindow/ActivityLog.h"
#include "ui/systems/AppState.h"
#include "ui/systems/DisplayStateManager.h"
#include "ui/systems/WindowRegistry.h"
#include "ui/systems/ProjectEventBus.h"

class QAction;
class QButtonGroup;
class QDialog;
class QFrame;
class QLabel;
class QSplitter;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPixmap;
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
}

namespace dolphin::ui {

struct ImportDialogResult;

class BottomDockPanel;
class ExecutionController;
class ExecutionProgressDialog;
class GeodesyPanel;
class InspectorPanel;
class RightPanelHost;
class LayerPickerWidget;
class LineListPanel;
class MapView;
class MapViewportHost;
class NodeGraphWindow;
class ProcessingController;
class SidescanViewController;
class SubBottomWindow;
class WaterfallWindow;

class CorrectionBatchOperator;
class ProjectOperationCoordinator;
class ViewportCoordinator;
class ConversationPanel;
class CommandBar;
class PanelChatWidget;
class DataLibraryWindow;
class GainControlPanel;
class HeadingInfoPanel;
class ImagingControlPanel;
class MainStatusBar;
class NavInfoPanel;
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
    // Project — thin delegates to m_session_ctrl
    void onNewProject()        { m_session_ctrl->newProject(); }
    void onOpenProject()       { m_session_ctrl->openProject(); }
    void onSaveProject()       { m_session_ctrl->saveProject(); }
    void onSaveProjectAs()     { m_session_ctrl->saveProjectAs(); }
    void onOpenProjectFolder() { m_session_ctrl->openProjectFolder(); }
    void onCloseProject()      { m_session_ctrl->closeProject(); }
    void onDeleteProject()     { m_session_ctrl->deleteProject(); }
    void onRenameProject();
    void onImportFile();
    void onGeodeticSettings();

    // Export stubs
    void onExportCsv();
    void onExportGeotiff();
    void onExportKmz();
    void onExportNav();
    void onExportPdf();
    void onExportScreenshot();
    void onExportManagerOpen();              // project-wide export hub window
    void exportContactsReport(bool docx);    // all contacts → PDF / Word via ContactReport

    // Processing
    void onRunAllLayers();
    void onRunSelectedLayer();
    void onBakeCorrections();   // explicit commit of gain/imaging corrections to .dlpd
    void onOpenProcessingWindow();

    // Tool stubs
    void onToolCursor();
    void onToolSelect();
    void onToggle3D();
    void onToolZoom();
    void onToolMeasure();
    void onMeasurementUpdated(double metres);
    void onContactPickedOnMap(double lon, double lat);
    void onWaterfallOpen();
    void onSubBottomOpen();
    void onContactManagerOpen();
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
                                   int channel_idx,
                                   const QPixmap& snapshot);
    void onWaterfallParamsApplied();
    void onWaterfallSetCrs(const std::string& from_layer_id);
    void onWaterfallNavProcessLine(dolphin::ui::NavProcessingParams params);
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
    bool exportRasterLayer(app::DataLayer* layer, const QString& path);   // raster -> GeoTIFF
    void onMergeLayers(const std::vector<std::string>& layer_ids);
    void onRemoveContact(uint64_t contact_id);
    void onRevealSource(const std::string& source_id);
    void updateControlsForModality(const app::DataLayer* layer);
    void displayRaster(app::DataLayer* layer);   // depth -> 3D terrain; visual/2D -> overlay
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

    void rebuildRecentMenu();
    // Display name for a recent-project path: the open project's (live) name when it
    // matches, else the name stored in the .dlp, falling back to the file basename.
    QString recentDisplayName(const QString& path) const;
    void bindProjectUi();
    // Set the active sensor/modality tab and apply the matching filter to the inspector.
    void refreshSensorTab(app::Modality m);
    void showImportDialog(const QStringList& paths,
                          const std::vector<core::ArtifactType>& module_filter = {});
    // Detect-then-confirm import. `preset` pre-checks a modality (from a modality-
    // specific menu command); empty = pre-check everything each file contains.
    void importFilesWithPreset(const std::vector<core::ArtifactType>& preset);
    void importRasterFiles();              // dedicated GDAL raster import path
    void createSessionProject();           // open a temp session project if none is current
    bool ensureProjectForImport(const ImportDialogResult& res);
    QList<CommandPaletteItem> buildCommandItems();
    void applyWorkspaceState(int panel_id, bool props_open, bool toolbar_visible);
    void setPropertiesOpen(bool open);
    void setRightToolBarVisible(bool visible);
    bool rightToolBarVisible() const;
    bool panelUsesContextStack(int panel_id) const;
    int  normalizePanelId(int panel_id) const;
    int  rightDockWidth() const;
    void updateContextInfo();
    // Status-bar cursor readout, reprojected into the project working grid so the
    // map, waterfall and sub-bottom viewers all report in the same survey CRS.
    void showCursorPosition(double lat, double lon, bool is_projected);
    void appendJobMessage(const QString& message);
    void updateActionStates();
    void refreshInspectorModalities();

    // SBP navigation corrections (Navigation / Geometry under the SBP sensor tab).
    // applySbpNavTo* store params per layer, refresh the SBP window, and rebuild
    // the affected map profile(s); buildSbpProfileMap is the shared rebuild.
    void applySbpNavToLine(const dolphin::ui::NavProcessingParams& p);
    void applySbpNavToAll (const dolphin::ui::NavProcessingParams& p);
    void buildSbpProfileMap(app::DataLayer* layer);
    // Re-apply (or clear) the stored nav corrections for a layer on SBP open.
    void applyStoredSbpNavParams(const std::string& layer_id);

    QWidget* makeContextPlaceholder(const QString& title, const QString& body);
    void     refreshSidebarSections(const QStringList& paths);
    void     refreshRecycleBin();

    DiagnosticsHub*   m_diag_hub     = nullptr;
    BottomDockPanel*  m_bottom_panel = nullptr;

    // Project session state is owned by PSC; use these helpers throughout aspect files.
    ProjectSessionController* m_session_ctrl = nullptr;
    // Returns the raw project pointer — null when no project is open.
    app::Project* currentProject() const noexcept;
    // Returns the shared_ptr — use when callers need ownership (setProject calls).
    std::shared_ptr<app::Project> currentProjectPtr() const noexcept;
    bool isProjectDirty()  const noexcept;
    // Mark dirty + emit windowTitleChanged via PSC.
    void markProjectDirty();

    // Layer selection state is owned by m_layer_ctrl; use this helper.
    LayerDisplayCoordinator* m_layer_ctrl = nullptr;
    const std::string& activeLayerId() const noexcept;

    app::OperationManager*  m_op_mgr = nullptr;  // central job runner + cancel registry
    app::ImportService*     m_import_service     = nullptr;
    app::ImportJobManager*  m_import_job_mgr     = nullptr;
    app::ProcessingService* m_processing_service = nullptr;

    // Maps import layer_id → DiagnosticsHub job_id for active indexing jobs.
    std::unordered_map<std::string, uint32_t> m_import_job_ids;
    // Maps OperationManager op_id → DiagnosticsHub job_id for bridge.
    std::unordered_map<uint32_t, uint32_t>    m_op_job_ids;

    SidescanViewController*  m_sss_ctrl    = nullptr;
    ExecutionController*     m_import_ctrl = nullptr;
    ProcessingController*    m_proc_ctrl   = nullptr;
    CorrectionBatchOperator*      m_corr_op       = nullptr;
    ProjectOperationCoordinator*  m_op_coord      = nullptr;
    ViewportCoordinator*          m_viewport_coord = nullptr;

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
    QWidget*        m_context_divider  = nullptr;  // 1px line at the panel's right edge
    QWidget*        m_left_edge_strip  = nullptr;
    QWidget*        m_right_edge_strip = nullptr;
    QFrame*         m_props_panel      = nullptr;
    QFrame*         m_right_tool_bar   = nullptr;
    int                 m_active_panel   = 0;
    bool                m_panel_open     = false;
    QPropertyAnimation* m_panel_anim     = nullptr;

    // True while the active layer's map build is driving the determinate status-bar
    // progress bar (so refreshLoadingIndicator doesn't override it with the spinner).
    bool                m_map_progress_active = false;

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
    QSplitter*          m_props_splitter      = nullptr;
    QListWidget*        m_props_history_list  = nullptr;
    QPropertyAnimation* m_props_anim          = nullptr;

    // Size the upper (Properties/Chats/History) pane to its current content so
    // the sensor shell rides up directly beneath it instead of leaving a gap.
    void adjustPropsSplit();

    ActivityLog m_activity_log;
    void rebuildHistoryList();
    void recordActivity(ActivityKind kind, const QString& description);

    // Legacy floating layer picker. The main shell now uses the fixed File
    // Explorer dock instead, so this typically remains null.
    LayerPickerWidget*      m_layer_picker   = nullptr;

    // Import progress overlay (bottom-centre of viewport)
    ExecutionProgressDialog* m_import_overlay = nullptr;

    // Inspector (lives in Properties overlay)
    InspectorPanel*    m_inspector     = nullptr;
    // Modal host — sensor-specific modules (Display, Radiometry, SBP tools)
    // Lives in the lower half of the properties panel, always visible.
    RightPanelHost*    m_modal_host    = nullptr;

    // Panel pointers sourced from RightPanelHost — used for viewer signal wiring.
    // Navigation / Geometry are per-modality: SSS pair drives the waterfall,
    // SBP pair drives the sub-bottom window.
    NavInfoPanel*        m_nav_panel         = nullptr;  // SSS Navigation
    HeadingInfoPanel*    m_heading_panel     = nullptr;  // SSS Geometry
    NavInfoPanel*        m_sbp_nav_panel     = nullptr;  // SBP Navigation
    HeadingInfoPanel*    m_sbp_heading_panel = nullptr;  // SBP Geometry
    GainControlPanel*    m_gain_panel        = nullptr;
    ImagingControlPanel* m_imaging_panel     = nullptr;

    QScrollArea*        m_props_scroll    = nullptr;
    PanelChatWidget*    m_chat_widget     = nullptr;

    // Sensor/modality tab bar (bottom of Properties page, outside scroll area)
    QWidget*     m_sensor_bar = nullptr;
    QToolButton* m_tab_sss   = nullptr;
    QToolButton* m_tab_sbp   = nullptr;
    QToolButton* m_tab_map   = nullptr;
    QToolButton* m_tab_mag   = nullptr;

    // Fixed left dock — project / files / layers / contacts tree
    LineListPanel*    m_line_list                = nullptr;
    QLabel*           m_context_title            = nullptr;
    QToolButton*      m_context_collapse_btn     = nullptr;
    bool              m_context_collapsed        = false;
    QListWidget*      m_sidebar_recent_list      = nullptr;
    QListWidget*      m_recycle_list             = nullptr;   // contact recycle bin (sidebar)

    // Geodesy standalone window (lazy-created on first open)
    QDialog*          m_geodesy_win    = nullptr;
    GeodesyPanel*     m_geodesy_panel  = nullptr;

    // Core systems — single instances owned by MainWindow.
    AppState*            m_app_state        = nullptr;
    DisplayStateManager* m_display_state    = nullptr;
    WindowRegistry*      m_window_registry  = nullptr;
    ProjectEventBus*     m_event_bus        = nullptr;
    // (task tracking absorbed by m_op_mgr — see OperationManager)

    // Per-layer display + nav state live on the model (DataLayer::sss_display_state
    // / sbp_display_state / nav_state) as the single source of truth, so
    // MainWindow holds no per-layer state maps.

    // SBP profile-map builds run through OperationManager keyed by
    // "sbp_profile:<layer-id>", so supersession (not a MainWindow set) prevents
    // duplicate in-flight builds for a layer.

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
    QPointer<QWidget>  m_contact_mgr_win;            // ContactManagerWindow (lazy-created)
    QPointer<QWidget>  m_export_win;                 // ExportManagerWindow (lazy-created)

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

    // Status bar
    MainStatusBar* m_status_bar = nullptr;
};

} // namespace dolphin::ui

using MainWindow = dolphin::ui::MainWindow;
