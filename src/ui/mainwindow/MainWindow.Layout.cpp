// MainWindow.Layout.cpp — panel toggles, window geometry, and status bar helpers.
// Maximum entries kept in the in-memory activity log (History panel).
static constexpr int kActivityLogMaxEntries = 1000;
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
#include <QStackedWidget>
#include <QToolButton>
#include <QUndoStack>
#include "ui/mainwindow/panels/InspectorPanel.h"

namespace dolphin::ui {

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
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

    if (!m_project) { m_status_bar->clearContext(); return; }

    const QString project = QString::fromStdString(m_project->name());
    QString layer;
    if (!m_active_layer_id.empty()) {
        if (auto* l = m_project->findLayer(m_active_layer_id))
            layer = QString::fromStdString(l->label);
    }
    m_status_bar->setProjectContext(project, layer);
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
}

void MainWindow::rebuildHistoryList()
{
    if (!m_props_history_list) return;
    m_props_history_list->clear();

    if (m_activity_log.empty()) {
        auto* hint = new QListWidgetItem(
            tr("No activity yet.\nImport a file, run processing, or adjust display settings."),
            m_props_history_list);
        hint->setFlags(Qt::NoItemFlags);
        hint->setData(Qt::UserRole + 2, true);
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    QString curSection;

    for (const auto& entry : m_activity_log) {
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
    m_activity_log.insert(m_activity_log.begin(),
        {kind, description, QDateTime::currentDateTime()});
    if (m_activity_log.size() > kActivityLogMaxEntries)
        m_activity_log.resize(kActivityLogMaxEntries);
    // Refresh the list only if the History tab is currently visible.
    if (m_props_stack && m_props_stack->currentIndex() == 2)
        rebuildHistoryList();
}

void MainWindow::onLayerVisibilityChanged(const std::string& layer_id, bool visible)
{
    if (!m_project) {
        if (m_viewport_host) m_viewport_host->setLayerVisible(layer_id, visible);
        else if (m_map_view) m_map_view->setLayerVisible(layer_id, visible);
        return;
    }

    // Determine previous state from the layer so undo knows what to restore.
    bool old_visible = visible;
    if (const auto* layer = m_project->findLayer(layer_id))
        old_visible = layer->visible;

    auto apply = [this](const std::string& lid, bool v) {
        if (auto* layer = m_project ? m_project->findLayer(lid) : nullptr)
            layer->visible = v;
        if (m_viewport_host) m_viewport_host->setLayerVisible(lid, v);
        else if (m_map_view) m_map_view->setLayerVisible(lid, v);
        if (m_line_list)     m_line_list->setLayerVisibility(lid, v);
        if (m_layer_picker)  m_layer_picker->setLayerVisibility(lid, v);
        // visible is serialized; direct mutation bypasses Project::modified() signal.
        m_project_dirty = true;
        setWindowTitleFromProject();
    };

    m_undo_stack->push(new SetLayerVisibleCommand(
        layer_id, old_visible, visible, std::move(apply)));

    if (m_project) {
        if (const auto* layer = m_project->findLayer(layer_id)) {
            recordActivity(ActivityKind::Visibility,
                tr("%1 %2").arg(QString::fromStdString(layer->label),
                               visible ? tr("shown") : tr("hidden")));
        }
    }
}

void MainWindow::updateActionStates()
{
    const bool has_project = m_project != nullptr;
    const bool has_layer   = has_project && !m_active_layer_id.empty();

    if (m_act_save)      m_act_save->setEnabled(has_project);
    if (m_act_run_all)   m_act_run_all->setEnabled(has_project);
    if (m_export_btn)    m_export_btn->setEnabled(has_project);
    // m_act_run_layer is capability-based; managed by updateControlsForModality.

    updateNavigationButtons();
}

} // namespace dolphin::ui
