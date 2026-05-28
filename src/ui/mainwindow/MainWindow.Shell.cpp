// MainWindow.Shell.cpp — setupCentralWidget.
// Panel construction is split across companion files:
//   MainWindow.ContextPanels.cpp  — buildContextPanel, makeContextPlaceholder
//   MainWindow.MainArea.cpp       — buildMainArea, buildPropertiesPanel
//   MainWindow.ToolBar.cpp        — buildActivityBar, buildRightToolBar, setupToolBar
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/bottom/BottomDockPanel.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapView3D.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/project/Project.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

namespace dolphin::ui {

static constexpr int kEdgeStripW    = 14;  // collapse/expand strip width
static constexpr int kCollapseBtnH  = 36;  // tall narrow toggle button height

void MainWindow::setupCentralWidget()
{
    auto* root = new QWidget(this);
    root->setObjectName("shellRoot");
    setCentralWidget(root);

    auto* root_layout = new QHBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    buildContextPanel(root);

    // ── Left edge strip — collapse/expand toggle for the context panel ────────
    m_left_edge_strip = new QWidget(root);
    m_left_edge_strip->setObjectName("leftEdgeStrip");
    m_left_edge_strip->setFixedWidth(kEdgeStripW);
    auto* left_strip_l = new QVBoxLayout(m_left_edge_strip);
    left_strip_l->setContentsMargins(0, 0, 0, 0);
    m_context_collapse_btn = new QToolButton(m_left_edge_strip);
    m_context_collapse_btn->setText("‹");  // ‹ — collapse left
    m_context_collapse_btn->setObjectName("panelEdgeBtnLeft");
    m_context_collapse_btn->setFixedSize(kEdgeStripW, kCollapseBtnH);
    m_context_collapse_btn->setToolTip(tr("Collapse panel"));
    connect(m_context_collapse_btn, &QToolButton::clicked,
            this, &MainWindow::onToggleContextPanel);
    left_strip_l->addStretch(1);
    left_strip_l->addWidget(m_context_collapse_btn);
    left_strip_l->addStretch(1);

    root_layout->addWidget(m_context_stack);
    root_layout->addWidget(m_left_edge_strip);
    root_layout->addWidget(buildMainArea(root), 1);

    buildPropertiesPanel(root);

    // ── Right edge strip — collapse/expand toggle for the properties panel ────
    m_right_edge_strip = new QWidget(root);
    m_right_edge_strip->setObjectName("rightEdgeStrip");
    m_right_edge_strip->setFixedWidth(kEdgeStripW);
    auto* right_strip_l = new QVBoxLayout(m_right_edge_strip);
    right_strip_l->setContentsMargins(0, 0, 0, 0);
    m_props_collapse_btn = new QToolButton(m_right_edge_strip);
    m_props_collapse_btn->setText("›");  // › — collapse right
    m_props_collapse_btn->setObjectName("panelEdgeBtnRight");
    m_props_collapse_btn->setFixedSize(kEdgeStripW, kCollapseBtnH);
    m_props_collapse_btn->setToolTip(tr("Collapse panel"));
    connect(m_props_collapse_btn, &QToolButton::clicked,
            this, &MainWindow::onTogglePropertiesPanel);
    right_strip_l->addStretch(1);
    right_strip_l->addWidget(m_props_collapse_btn);
    right_strip_l->addStretch(1);

    root_layout->addWidget(m_right_edge_strip);
    root_layout->addWidget(m_props_panel);
    m_right_tool_bar = buildRightToolBar(root);
    root_layout->addWidget(m_right_tool_bar);

    // Empty-state call-to-action buttons in the left panel
    connect(m_line_list, &LineListPanel::newProjectRequested,
            this, &MainWindow::onNewProject);
    connect(m_line_list, &LineListPanel::importFilesRequested,
            this, &MainWindow::onImportFile);
    connect(m_line_list, &LineListPanel::recentProjectRequested,
            this, [this](const QString& path) { loadProject(path.toStdString()); });

    // Import hint button on the empty map canvas
    connect(m_viewport_host, &MapViewportHost::importFilesRequested,
            this, &MainWindow::onImportFile);

    connect(m_line_list, &LineListPanel::layerSelected,
            this, &MainWindow::onLayerSelected);
    connect(m_line_list, &LineListPanel::contactSelected,
            this, &MainWindow::onContactSelected);
    connect(m_line_list, &LineListPanel::layerVisibilityChanged,
            this, &MainWindow::onLayerVisibilityChanged);
    connect(m_line_list, &LineListPanel::layerMultiSelected,
            this, [this](const std::vector<std::string>& ids) {
        if (m_map_view)       m_map_view->setSelectedLayers(ids);
        if (m_viewport_host)  m_viewport_host->setSelectedLayers(ids);
    });
    connect(m_line_list, &LineListPanel::navTrackVisibilityChanged,
            this, [this](const std::string& id, bool visible) {
        if (m_map_view) m_map_view->setNavTrackVisible(id, visible);
    });

    // ── Context menu actions ──────────────────────────────────────────────────
    connect(m_line_list, &LineListPanel::openInWaterfallRequested,
            this, [this](const std::string& id) {
        onLayerSelected(id);
        onWaterfallOpen();
    });
    connect(m_line_list, &LineListPanel::openInSubBottomRequested,
            this, [this](const std::string& id) {
        onLayerSelected(id);
        onSubBottomOpen();
    });
    connect(m_line_list, &LineListPanel::runLayerRequested,
            this, [this](const std::string& id) {
        onLayerSelected(id);
        onRunSelectedLayer();
    });
    connect(m_line_list, &LineListPanel::removeLayerRequested,
            this, &MainWindow::onRemoveLayer);
    connect(m_line_list, &LineListPanel::removeContactRequested,
            this, &MainWindow::onRemoveContact);
    connect(m_line_list, &LineListPanel::exportContactsRequested,
            this, &MainWindow::onExportCsv);
    connect(m_line_list, &LineListPanel::revealInExplorerRequested,
            this, &MainWindow::onRevealSource);
    connect(m_line_list, &LineListPanel::renameLayerRequested,
            this, &MainWindow::onRenameLayer);
    connect(m_line_list, &LineListPanel::removeLayersRequested,
            this, &MainWindow::onRemoveLayers);
    connect(m_line_list, &LineListPanel::runLayersRequested,
            this, &MainWindow::onRunLayers);
    connect(m_line_list, &LineListPanel::exportLayersRequested,
            this, &MainWindow::onExportLayers);
    connect(m_line_list, &LineListPanel::mergeLayersRequested,
            this, &MainWindow::onMergeLayers);

    connect(m_line_list, &LineListPanel::activityLogged,
            this, [this](const QString& desc, int kind) {
        recordActivity(static_cast<ActivityKind>(kind), desc);
    });

    connect(m_line_list, &LineListPanel::sourceSelected,
            this, [this](const std::string& src_id) {
        if (m_project) {
            auto* src = m_project->findSource(src_id);
            const auto layers = m_project->findLayersBySource(src_id);
            if (!layers.empty())
                onLayerSelected(layers.front()->id);
            if (src) appendJobMessage(
                QString::fromStdString(src->path) + "  \u00b7  " +
                QString::fromStdString(src->format).toUpper());
        }
    });

    if (m_bottom_panel) {
        connect(m_bottom_panel, &BottomDockPanel::layerFocusRequested,
                this, [this](const QString& layer_id) {
                    onLayerSelected(layer_id.toStdString());
                });
    }

    connect(m_inspector, &InspectorPanel::paletteChanged,
            this,        &MainWindow::onPaletteChanged);

    // Properties panel tab bar
    connect(m_props_tab_tools,   &QToolButton::clicked, this, [this]{ onPropsTabChanged(0); });
    connect(m_props_tab_chats,   &QToolButton::clicked, this, [this]{ onPropsTabChanged(1); });
    connect(m_props_tab_history, &QToolButton::clicked, this, [this]{ onPropsTabChanged(2); });

    // History list — clicking a layer row navigates to it
    if (m_props_history_list) {
        connect(m_props_history_list, &QListWidget::itemActivated,
                this, [this](QListWidgetItem* item) {
            if (item->data(Qt::UserRole + 2).toBool()) return;  // skip section headers
            const QString id = item->data(Qt::UserRole).toString();
            if (!id.isEmpty()) onLayerSelected(id.toStdString());
        });
    }

    connect(m_map_view, &MapView::contextMenuRequested,
            this,       &MainWindow::onMapContextMenu);
    connect(m_viewport_host, &MapViewportHost::contextMenuRequested,
            this,            &MainWindow::onMapContextMenu);

    // Single click on a swath (2D or 3D) → activate that layer.
    connect(m_map_view, &MapView::layerClicked,
            this, [this](const std::string& id) { onLayerSelected(id); });
    connect(m_viewport_host, &MapViewportHost::layerClicked,
            this, [this](const std::string& id) { onLayerSelected(id); });

    // Multi-select → sync the line list (2D and 3D both feed the same list).
    connect(m_map_view, &MapView::layersSelected,
            this, [this](const std::vector<std::string>& ids) {
        if (m_line_list) m_line_list->setSelectedLayers(ids);
    });
    connect(m_viewport_host, &MapViewportHost::layersSelected,
            this, [this](const std::vector<std::string>& ids) {
        if (m_line_list) m_line_list->setSelectedLayers(ids);
    });

    connect(m_viewport_host, &MapViewportHost::cursorMoved,
            this, [this](double lon, double lat) {
        if (std::isnan(lon)) {
            m_status_bar->clearCursorPosition();
            m_status_bar->clearCursorDepth();
            return;
        }
        m_status_bar->setCursorPosition(lat, lon, m_map_view->isProjected());
    });

    connect(m_viewport_host, &MapViewportHost::gpuInfo,
            this, [this](const QString& renderer, const QString& version, int max_tex) {
        appendJobMessage(
            tr("GPU: %1 | OpenGL %2 | Max texture: %3 px")
                .arg(renderer, version)
                .arg(max_tex));
    });

    connect(m_viewport_host, &MapViewportHost::glInitError,
            this, [this](const QString& msg) {
        m_diag_hub->postProblem(msg, DiagnosticsHub::Severity::Error,
                                QStringLiteral("3d-view"));
    });

    connect(m_map_view, &MapView::measurementUpdated,
            this, &MainWindow::onMeasurementUpdated);

    connect(m_map_view, &MapView::contactPickedOnMap,
            this, &MainWindow::onContactPickedOnMap);

    connect(m_viewport_host, &MapViewportHost::contactPickedAt,
            this, &MainWindow::onContactPickedOnMap);
}

} // namespace dolphin::ui
