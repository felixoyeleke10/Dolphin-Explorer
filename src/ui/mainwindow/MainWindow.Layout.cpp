// MainWindow.Layout.cpp — panel toggles, window geometry, and status bar helpers.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/Features.h"
#include "ui/shell/Theme.h"
#include "ui/features/geodesy/GeodesyPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QWidget>
#include <algorithm>
#include "ui/mainwindow/panels/InspectorPanel.h"

namespace dolphin::ui {

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    adjustPropsSplit();   // keep the upper pane hugging its content as the window grows
}

// Make the upper (Properties/Chats/History) pane only as tall as the page it's
// currently showing, and hand all remaining height to the lower sensor shell.
// Short property lists no longer leave a dead gap above the sensor tabs; long
// content still scrolls inside the upper pane's own scroll area.
void MainWindow::adjustPropsSplit()
{
    if (!m_props_splitter || !m_props_stack) return;

    const int total = m_props_splitter->height();
    if (total < 120) return;   // not laid out yet — leave the provisional split

    const int handle = m_props_splitter->handleWidth();
    const int avail  = total - handle;

    // The lower sensor shell keeps a usable floor (tab bar + a couple of rows).
    const int lower_min = 160;

    int upper_want;
    if (m_props_stack->currentIndex() == 0 && m_inspector) {
        // Properties tab: header (tab bar) + the current page's real content.
        int header_h = 0;
        if (m_props_tab_tools && m_props_tab_tools->parentWidget())
            header_h = m_props_tab_tools->parentWidget()->sizeHint().height();
        upper_want = header_h + m_inspector->contentHeight() + 8 /* chrome */;
    } else {
        // Chats / History want room to work — give them the lion's share.
        upper_want = avail * 6 / 10;
    }

    const int upper_min = 64;
    const int upper_max = avail - lower_min;
    if (upper_max <= upper_min) {            // panel too short to split sensibly
        m_props_splitter->setSizes({ avail / 2, avail - avail / 2 });
        return;
    }
    upper_want = std::clamp(upper_want, upper_min, upper_max);
    m_props_splitter->setSizes({ upper_want, avail - upper_want });
}

bool MainWindow::panelUsesContextStack(int panel_id) const
{
    return panel_id == PanelExplorer;
}

int MainWindow::normalizePanelId(int panel_id) const
{
    Q_UNUSED(panel_id)
    return 0;
}

int MainWindow::rightDockWidth() const
{
    return (m_right_tool_bar && m_right_tool_bar->isVisible()) ? Theme::kToolBarW : 0;
}

void MainWindow::togglePanel(int panel_id, bool force_open)
{
    Q_UNUSED(force_open)

    if (panel_id == PanelWaterfall) {
        if (auto* btn = m_activity_btns.value(PanelWaterfall, nullptr))
            btn->setChecked(false);
        onWaterfallOpen();
        return;
    }

    if (!m_context_stack) return;

    if (panel_id == PanelGeodesy) {
        onGeodeticSettings();
        return;
    }

    if (panel_id == PanelSettings) {
        if (auto* btn = m_activity_btns.value(PanelSettings, nullptr))
            btn->setChecked(false);
        onAppSettings();
        return;
    }

    if constexpr (Features::kDataLibrary) {
        if (panel_id == PanelDataLibrary) {
            if (auto* btn = m_activity_btns.value(PanelDataLibrary, nullptr))
                btn->setChecked(false);
            onDataLibraryOpen();
            return;
        }
    }

    m_context_stack->setCurrentIndex(0);
}

void MainWindow::toggleProperties()
{
    setPropertiesOpen(!m_props_open);
}

void MainWindow::onViewTabChanged(int /*index*/)
{
    // Reserved — Phase 2
}

void MainWindow::setPropertiesOpen(bool open)
{
    if (!m_props_panel) return;
    m_props_open = open;
    m_props_panel->setVisible(open);
}

void MainWindow::setRightToolBarVisible(bool visible)
{
    if (m_right_tool_bar) m_right_tool_bar->setVisible(visible);
}

bool MainWindow::rightToolBarVisible() const
{
    return m_right_tool_bar && m_right_tool_bar->isVisible();
}

void MainWindow::applyWorkspaceState(int panel_id, bool props_open, bool toolbar_visible)
{
    setRightToolBarVisible(toolbar_visible);
    setPropertiesOpen(props_open);

    if (panel_id == PanelWaterfall)
        onWaterfallOpen();

    if (m_context_stack)
        m_context_stack->setCurrentIndex(0);
}

void MainWindow::updateContextInfo()
{
    if (!m_status_bar) return;

    if (!currentProject()) {
        m_status_bar->clearContext();
        m_status_bar->setViewCrs({});
        return;
    }

    // Status bar shows only the project name — the active line is already visible in
    // the tree (selected) and on the map, so repeating it here was just noise.
    const QString project = QString::fromStdString(currentProject()->name());
    m_status_bar->setProjectContext(project);

    // Status-bar CRS shows the project's survey/working grid (the projected CRS
    // the data is in) — not the map's internal WGS84 render ref — so it matches
    // the per-layer "Source CRS" in the inspector instead of contradicting it.
    // A project spanning multiple projected CRSes is flagged "(mixed)" so the
    // single badge doesn't imply a uniform grid (the dominant CRS is shown).
    const core::SpatialRef sr = currentProject()->workingCrs();
    QString crs_text = sr.id.empty() ? QStringLiteral("WGS 84")
                                     : QString::fromStdString(sr.id);
    if (currentProject()->hasMixedProjectedSources())
        crs_text += tr(" (mixed)");
    m_status_bar->setViewCrs(crs_text);
}

void MainWindow::appendJobMessage(const QString& message)
{
    if (m_status_bar) m_status_bar->showJobMessage(message);
    if (m_diag_hub)   m_diag_hub->logOutput(message);
}

void MainWindow::onToggleContextPanel()
{
    m_context_collapsed = !m_context_collapsed;

    if (m_context_stack)
        m_context_stack->setVisible(!m_context_collapsed);
    if (m_context_divider)
        m_context_divider->setVisible(!m_context_collapsed);

    // \u2039 = panel open (click to collapse); \u203a = panel hidden (click to expand)
    if (m_context_collapse_btn)
        m_context_collapse_btn->setText(m_context_collapsed ? "\u203a" : "\u2039");

    if (m_btn_primary_sidebar)
        m_btn_primary_sidebar->setChecked(!m_context_collapsed);
}

void MainWindow::onTogglePropertiesPanel()
{
    m_props_collapsed = !m_props_collapsed;

    if (m_props_panel)
        m_props_panel->setVisible(!m_props_collapsed);

    // › = panel open (click to collapse); ‹ = panel hidden (click to expand)
    if (m_props_collapse_btn)
        m_props_collapse_btn->setText(m_props_collapsed ? "‹" : "›");

    if (m_btn_secondary_sidebar)
        m_btn_secondary_sidebar->setChecked(!m_props_collapsed);
}

void MainWindow::onPropsTabChanged(int tab)
{
    if (m_props_stack) m_props_stack->setCurrentIndex(tab);
    if (tab == 2) rebuildHistoryList();  // refresh on every open
    adjustPropsSplit();                  // resize the pane to the newly shown tab
}

void MainWindow::rebuildHistoryList()
{
    if (!m_props_history_list) return;
    m_props_history_list->clear();

    if (m_activity_log.entries().empty()) {
        auto* hint = new QListWidgetItem(
            tr("No activity yet.\nImport a file, run processing, or adjust display settings."),
            m_props_history_list);
        hint->setFlags(Qt::NoItemFlags);
        hint->setData(Qt::UserRole + 4, true);   // empty-state hint (not a section header)
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    QString curSection;

    for (const auto& entry : m_activity_log.entries()) {
        // Rolling-window buckets — not calendar-day boundaries
        const qint64 age = entry.timestamp.secsTo(now);   // seconds ago
        const bool is_today = entry.timestamp.date() == now.date();

        QString section;
        if (is_today)
            section = tr("Today");
        else if (age < 24LL * 3600)
            section = tr("Last 24 Hours");
        else if (age < 7LL * 24 * 3600)
            section = tr("Last 7 Days");
        else
            section = entry.timestamp.date().toString("MMMM yyyy");

        if (section != curSection) {
            curSection = section;
            auto* hdr = new QListWidgetItem(section, m_props_history_list);
            hdr->setFlags(Qt::NoItemFlags);
            hdr->setData(Qt::UserRole + 2, true);
        }

        // Timestamps — section header already gives the range context, so keep these compact
        QString timeStr;
        if (is_today)
            timeStr = entry.timestamp.toString("h:mm AP");
        else if (age < 24LL * 3600)
            timeStr = entry.timestamp.toString("'Yesterday,' h:mm AP");
        else if (age < 7LL * 24 * 3600)
            timeStr = entry.timestamp.toString("ddd, h:mm AP");
        else
            timeStr = entry.timestamp.toString("d MMM yyyy");

        auto* item = new QListWidgetItem(entry.description, m_props_history_list);
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::UserRole + 1, timeStr);
        item->setData(Qt::UserRole + 2, false);
        item->setData(Qt::UserRole + 3, static_cast<int>(entry.kind));
    }
}

void MainWindow::recordActivity(ActivityKind kind, const QString& description)
{
    m_activity_log.record(kind, description);
    // Refresh the list only if the History tab is currently visible.
    if (m_props_stack && m_props_stack->currentIndex() == 2)
        rebuildHistoryList();
}

void MainWindow::onLayerVisibilityChanged(const std::string& layer_id, bool visible)
{
    if (!currentProject()) {
        if (m_viewport_host) m_viewport_host->setLayerVisible(layer_id, visible);
        else if (m_map_view) m_map_view->setLayerVisible(layer_id, visible);
        return;
    }

    // Determine previous state from the layer so undo knows what to restore.
    bool old_visible = visible;
    if (const auto* layer = currentProject()->findLayer(layer_id))
        old_visible = layer->visible;

    auto apply = [this](const std::string& lid, bool v) {
        if (auto* layer = currentProject() ? currentProject()->findLayer(lid) : nullptr)
            layer->visible = v;
        if (m_viewport_host) m_viewport_host->setLayerVisible(lid, v);
        else if (m_map_view) m_map_view->setLayerVisible(lid, v);
        if (m_line_list)     m_line_list->setLayerVisibility(lid, v);
        if (m_layer_picker)  m_layer_picker->setLayerVisibility(lid, v);
        // visible is serialized; direct mutation bypasses Project::modified() signal.
        markProjectDirty();
    };

    m_undo_stack->push(new SetLayerVisibleCommand(
        layer_id, old_visible, visible, std::move(apply)));

    if (currentProject()) {
        if (const auto* layer = currentProject()->findLayer(layer_id)) {
            recordActivity(ActivityKind::Visibility,
                tr("%1 %2").arg(QString::fromStdString(layer->label),
                               visible ? tr("shown") : tr("hidden")));
        }
    }
}

void MainWindow::updateActionStates()
{
    const bool has_project = currentProject() != nullptr;
    const bool has_layer   = has_project && !activeLayerId().empty();

    if (m_act_save)      m_act_save->setEnabled(has_project);
    if (m_act_run_all)   m_act_run_all->setEnabled(has_project);
    if (m_export_btn)    m_export_btn->setEnabled(has_project);
    // m_act_run_layer is capability-based; managed by updateControlsForModality.
}

} // namespace dolphin::ui
