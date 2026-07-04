// MainWindow.Shell.cpp — setupCentralWidget.
// Panel construction is split across companion files:
//   MainWindow.ContextPanels.cpp  — buildContextPanel, makeContextPlaceholder
//   MainWindow.MainArea.cpp       — buildMainArea, buildPropertiesPanel
//   MainWindow.ToolBar.cpp        — buildActivityBar, setupToolBar
#include "ui/mainwindow/MainWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/bottom/BottomDockPanel.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/shell/AppInfo.h"
#include "ui/systems/DisplayStateManager.h"
#include "app/project/Project.h"
#include "geo/GeoUtils.h"
#include "core/SpatialRef.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

static constexpr int kEdgeStripW    = 14;  // collapse/expand strip width
static constexpr int kCollapseBtnH  = 36;  // tall narrow toggle button height

// Update the status-bar cursor position from a viewer. Inputs may be geographic
// (WGS84 lat/lon, is_projected=false) or already-projected source coordinates
// (is_projected=true). Geographic input is reprojected into the project working
// grid when that grid is a transformable projected CRS, so the map, waterfall and
// sub-bottom readouts all report in the same survey grid (SonarWiz convention).
void MainWindow::showCursorPosition(double lat, double lon, bool is_projected)
{
    if (!m_status_bar) return;
    if (!is_projected) {
        if (auto* proj = currentProject()) {
            const core::SpatialRef wc = proj->workingCrs();
            double n = 0.0, e = 0.0;
            if (core::spatialRefIsProjected(wc) &&
                geo::latLonToProjected(lat, lon, wc, n, e)) {
                m_status_bar->setCursorPosition(n, e, /*is_projected=*/true);
                return;
            }
        }
    }
    m_status_bar->setCursorPosition(lat, lon, is_projected);
}

void MainWindow::setupCentralWidget()
{
    auto* root = new QWidget(this);
    root->setObjectName("shellRoot");
    setCentralWidget(root);

    auto* root_layout = makeCompactLayout<QHBoxLayout>(root);

    buildContextPanel(root);

    // -- Left edge strip — collapse/expand toggle for the context panel --------
    m_left_edge_strip = new QWidget(root);
    m_left_edge_strip->setObjectName("leftEdgeStrip");
    m_left_edge_strip->setFixedWidth(kEdgeStripW);
    auto* left_strip_l = makeCompactLayout<QVBoxLayout>(m_left_edge_strip);
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

    // 1px divider at the panel's right edge. A QSS border-right on the QStackedWidget
    // (#contextPanel) is unreliable — its page paints over it — so use a real widget,
    // mirroring the right panel's QFrame border-left which renders dependably.
    m_context_divider = new QWidget(root);
    m_context_divider->setObjectName("shellDivider");
    m_context_divider->setFixedWidth(1);

    root_layout->addWidget(m_context_stack);
    root_layout->addWidget(m_context_divider);
    root_layout->addWidget(m_left_edge_strip);
    root_layout->addWidget(buildMainArea(root), 1);

    buildPropertiesPanel(root);

    // -- Right edge strip — collapse/expand toggle for the properties panel ----
    m_right_edge_strip = new QWidget(root);
    m_right_edge_strip->setObjectName("rightEdgeStrip");
    m_right_edge_strip->setFixedWidth(kEdgeStripW);
    auto* right_strip_l = makeCompactLayout<QVBoxLayout>(m_right_edge_strip);
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

    // Empty-state call-to-action buttons in the left panel
    connect(m_line_list, &LineListPanel::newProjectRequested,
            this, &MainWindow::onNewProject);
    connect(m_line_list, &LineListPanel::importFilesRequested,
            this, &MainWindow::onImportFile);

    // Import hint button on the empty map canvas
    connect(m_viewport_host, &MapViewportHost::importFilesRequested,
            this, &MainWindow::onImportFile);
    connect(m_viewport_host, &MapViewportHost::newProjectRequested,
            this, &MainWindow::onNewProject);

    // Empty-map launcher: recent-project entries open directly from the canvas.
    // Deferred so the mouse-release unwinds before loadProject shows/hides
    // widgets (same Win32 blink guard as the old sidebar recent list).
    connect(m_viewport_host, &MapViewportHost::openProjectRequested,
            this, [this](const QString& path) {
                QTimer::singleShot(0, this, [this, path]() {
                    m_session_ctrl->openProjectPath(path.toStdString());
                });
            });
    {
        QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
        refreshSidebarSections(
            s.value(QStringLiteral("recentProjects")).toStringList());
    }

    // (Terrain/draping loads go exclusively through Views ▸ MAP ▸ Draping
    // surface / the 3D context menu — both call onChooseDrapingSurface.)

    connect(m_line_list, &LineListPanel::layerSelected,
            this, &MainWindow::onLayerSelected);
    connect(m_line_list, &LineListPanel::contactSelected,
            this, &MainWindow::onContactSelected);
    connect(m_line_list, &LineListPanel::featureSelected,
            this, [this](uint64_t id) {
        if (m_map_view) m_map_view->setSelectedFeature(id);
    });
    connect(m_line_list, &LineListPanel::layerVisibilityChanged,
            this, &MainWindow::onLayerVisibilityChanged);
    // Annotation visibility checkboxes — undoable, same spirit as layer toggles.
    connect(m_line_list, &LineListPanel::contactVisibilityChanged,
            this, [this](uint64_t id, bool visible) {
        if (!currentProject()) return;
        for (const auto& c : currentProject()->contacts())
            if (c.id == id) {
                if (c.visible == visible) return;
                core::Contact after = c;
                after.visible = visible;
                m_undo_stack->push(new UpdateContactCommand(currentProject(), c, after));
                return;
            }
    });
    connect(m_line_list, &LineListPanel::featureVisibilityChanged,
            this, [this](uint64_t id, bool visible) {
        if (!currentProject()) return;
        for (const auto& f : currentProject()->features())
            if (f.id == id) {
                if (f.visible == visible) return;
                core::Feature after = f;
                after.visible = visible;
                m_undo_stack->push(new UpdateFeatureCommand(currentProject(), f, after));
                return;
            }
    });
    connect(m_line_list, &LineListPanel::layerMultiSelected,
            this, [this](const std::vector<std::string>& ids) {
        if (m_viewport_host)  m_viewport_host->setSelectedLayers(ids);
        else if (m_map_view)  m_map_view->setSelectedLayers(ids);
        updateToolsApplyBar();   // keep the "Apply to Selected (N)" label in sync
    });
    connect(m_line_list, &LineListPanel::navTrackVisibilityChanged,
            this, [this](const std::string& id, bool visible) {
        if (m_viewport_host) m_viewport_host->setNavTrackVisible(id, visible);
        else if (m_map_view) m_map_view->setNavTrackVisible(id, visible);
    });

    // -- Context menu actions --------------------------------------------------
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
    connect(m_line_list, &LineListPanel::removeFeatureRequested,
            this, [this](uint64_t id) {
        if (currentProject())
            m_undo_stack->push(new RemoveFeatureCommand(currentProject(), id));
    });
    connect(m_line_list, &LineListPanel::exportContactsRequested,
            this, &MainWindow::onExportCsv);
    connect(m_line_list, &LineListPanel::revealInExplorerRequested,
            this, &MainWindow::onRevealSource);
    connect(m_line_list, &LineListPanel::renameLayerRequested,
            this, &MainWindow::onRenameLayer);
    connect(m_line_list, &LineListPanel::renameProjectRequested,
            this, &MainWindow::onRenameProject);
    connect(m_line_list, &LineListPanel::saveProjectRequested,
            this, &MainWindow::onSaveProject);
    connect(m_line_list, &LineListPanel::saveProjectAsRequested,
            this, &MainWindow::onSaveProjectAs);
    connect(m_line_list, &LineListPanel::openProjectFolderRequested,
            this, &MainWindow::onOpenProjectFolder);
    connect(m_line_list, &LineListPanel::closeProjectRequested,
            this, &MainWindow::onCloseProject);
    connect(m_line_list, &LineListPanel::deleteProjectRequested,
            this, &MainWindow::onDeleteProject);
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
        if (currentProject()) {
            auto* src = currentProject()->findSource(src_id);
            const auto layers = currentProject()->findLayersBySource(src_id);
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

    connect(m_modal_host, &RightPanelHost::paletteChanged,
            this,         &MainWindow::onPaletteChanged);
    // NOTE: the status-bar palette picker is wired in setupStatusBar() — m_status_bar
    // does not exist yet here (setupCentralWidget runs before setupStatusBar).

    // Properties panel tab bar — handled by PanelTabBar::tabChanged, wired in
    // buildPropertiesPanel().

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
        if (std::isnan(lon))
            return;  // cursor left the viewport — freeze the last coordinate
        // Map renders in WGS84; report the readout in the project working grid.
        showCursorPosition(lat, lon, /*is_projected=*/false);
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

    connect(m_map_view, &MapView::featureDrawn,
            this, &MainWindow::onFeatureDrawn);

    // Double-click a layer's coverage on the mosaic → open its viewer (SSS → waterfall,
    // SBP → sub-bottom), mirroring a double-click in the left panel line list.
    connect(m_map_view, &MapView::layerActivated,
            this, [this](const std::string& id) {
        if (!currentProject()) return;
        const auto* layer = currentProject()->findLayer(id);
        if (!layer) return;
        onLayerSelected(id);
        if (layer->modality == app::Modality::SubBottom)      onSubBottomOpen();
        else if (layer->modality == app::Modality::Sidescan)  onWaterfallOpen();
    });

    connect(m_viewport_host, &MapViewportHost::contactPickedAt,
            this, &MainWindow::onContactPickedOnMap);
}

} // namespace dolphin::ui
