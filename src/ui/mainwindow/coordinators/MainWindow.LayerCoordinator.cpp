// MainWindow.LayerCoordinator.cpp — layer selection and activity-panel coordination.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/Features.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/track/TrackMapBuild.h"
#include "ui/features/map/subbottom/SbpProfileBuild.h"
#include "app/services/ImportService.h"
#include "ui/features/map/subbottom/SubBottomMapDiagnostics.h"
#include "ui/bottom/DiagnosticsHub.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "app/project/Project.h"
#include "app/layers/CapabilitySet.h"
#include "app/layers/DataLayer.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QUndoStack>
#include <QWidget>
#include <QtConcurrent>

#include <algorithm>

namespace dolphin::ui {

void MainWindow::onLayerSelected(const std::string& layer_id)
{
    if (!m_project) return;
    if (!m_replaying_navigation)
        recordNavigationSelection(layer_id);

    m_active_layer_id = layer_id;

    auto* layer = m_project->findLayer(layer_id);

    // Publish the new selection so any subscriber (panels, future tools) can
    // react without coupling directly to MainWindow.
    m_app_state->setSelection({
        layer_id,
        layer ? layer->modality : app::Modality::Unknown
    });

    // Pre-insert a Profile placeholder for SBP layers so rebuildNavTrack()
    // inside setActiveLayer() skips its synchronous build while the async
    // trace load runs below.
    if (m_map_view && layer
            && layer->modality == app::Modality::SubBottom
            && layer->index_built && !layer->artifact_index.empty()) {
        const auto* existing = m_map_view->layerData(layer_id);
        if (!existing || existing->nav_track.empty()) {
            LayerMapData ph;
            ph.kind           = LayerMapKind::Profile;
            ph.show_nav_track = true;
            m_map_view->setLayerMapData(layer_id, std::move(ph));
        }
    }

    if (m_map_view)
        m_map_view->setActiveLayer(layer_id);
    if (m_viewport_host)
        m_viewport_host->setActiveLayer(layer_id);

    if (layer) {
        using M = app::Modality;
        const M mod = layer->modality;

        // Sidescan: full background swath build via the SSS controller.
        if (m_sss_ctrl && mod == M::Sidescan)
            m_sss_ctrl->activateLayer(layer_id, m_project.get());

        // MAG / MBE: rebuildNavTrack (called by setActiveLayer) builds the track.
        else if (m_map_view && (mod == M::Magnetometer || mod == M::Multibeam))
            m_map_view->setNavTrackVisible(layer_id, true);

        // SubBottom: loads full trace data then builds a Profile LayerMapData
        // (nav track + per-trace bottom-depth scalar for the colored map ribbon).
        // Skipped if real map data (non-empty nav_track) is already present for
        // this layer — avoids redundant disk reads on re-selection.
        else if (m_map_view && mod == M::SubBottom && m_import_service) {
            if (layer->index_built && !layer->artifact_index.empty()) {
                const auto* existing = m_map_view->layerData(layer_id);
                if ((!existing || existing->nav_track.empty())
                        && !m_pending_sbp_builds.count(layer_id)) {
                    core::SpatialRef source_crs = layer->source_spatial_ref;
                    if (source_crs.empty()) {
                        if (const auto* src = m_project->findSource(layer->source_id))
                            source_crs = src->source_spatial_ref;
                    }
                    const core::SpatialRef display_crs = m_project->displaySpatialRef();

                    // Capture fields by value — DataLayer must not be accessed on bg thread.
                    const std::string store_path   = layer->artifact_store_path;
                    const std::string store_format = layer->artifact_store_format;
                    const core::ArtifactIndex index_copy = layer->artifact_index;
                    std::string source_path;
                    if (const auto* src = m_project->findSource(layer->source_id))
                        source_path = src->path;
                    const std::string lid = layer_id;
                    auto* svc = m_import_service;

                    m_pending_sbp_builds.insert(layer_id);
                    auto* watcher = new QFutureWatcher<LayerMapData>(this);
                    connect(watcher, &QFutureWatcher<LayerMapData>::finished, this,
                            [this, watcher, lid]() {
                                LayerMapData result = watcher->result();
                                watcher->deleteLater();
                                m_pending_sbp_builds.erase(lid);

                                if (m_map_view) {
                                    result.track_stats.layer_visible =
                                        m_map_view->isLayerVisible(lid);
                                    m_map_view->setLayerMapData(lid, result);
                                }

                                if (m_diag_hub)
                                    postSubBottomMapDiagnostics(
                                        m_diag_hub,
                                        QString::fromStdString(lid),
                                        result.track_stats);
                            });
                    watcher->setFuture(QtConcurrent::run(
                        [svc, store_path, store_format, index_copy,
                         source_path, source_crs, display_crs]() {
                            const auto traces = svc->loadAllSubBottomTraces(
                                store_path, store_format, index_copy, source_path);
                            return buildSbpProfileMapData(traces, source_crs, display_crs);
                        }));
                }
            }
        }
    }

    if (m_inspector)
        layer ? m_inspector->showLayer(layer) : m_inspector->showEmpty();

    // Always bring the Properties tab into view when a layer is selected.
    if (layer && m_props_stack && m_props_stack->currentIndex() != 0) {
        m_props_stack->setCurrentIndex(0);
        if (m_props_tab_tools) m_props_tab_tools->setChecked(true);
    }


    if (m_waterfall_win && m_waterfall_win->isVisible()) {
        if (layer && m_waterfall_win->currentLayerId() != layer->id) {
            const auto* src = m_project->findSource(layer->source_id);
            const std::string path = src ? src->path : std::string{};
            const uint64_t    sz   = src ? src->size_bytes : 0;
            m_waterfall_win->setLayer(layer, m_import_service, path, sz);
            applyStoredNavParams(layer->id);
            const auto wf_it = m_layer_wf_params.find(layer->id);
            if (wf_it != m_layer_wf_params.end())
                m_waterfall_win->applyExternalParams(wf_it->second);
        } else if (!layer) {
            m_waterfall_win->clearLayer();
        }
    }

    if (m_sbp_win && m_sbp_win->isVisible()) {
        if (layer && layer->modality == app::Modality::SubBottom) {
            if (m_sbp_win->currentLayerId() != layer->id) {
                const auto* src = m_project->findSource(layer->source_id);
                const std::string path = src ? src->path : std::string{};
                const uint64_t    sz   = src ? src->size_bytes : 0;
                m_sbp_win->setLayer(layer, m_import_service, path, sz);
            }
        } else {
            m_sbp_win->clearLayer();
        }
    }

    if constexpr (Features::kNodeGraph) {
        if (m_node_graph_win && m_node_graph_win->isVisible())
            m_node_graph_win->setLayer(layer, m_project.get());
    }

    if (m_line_list) m_line_list->setActiveLayer(layer_id);

    updateControlsForModality(layer);
    updateActionStates();
    updateContextInfo();
}

void MainWindow::updateControlsForModality(const app::DataLayer* layer)
{
    const app::Modality m = layer ? layer->modality : app::Modality::Unknown;
    const app::CapabilitySet caps = app::capabilitiesFor(m);

    // Enable/disable the per-layer processing action based on capability.
    // m_act_run_all is project-level and controlled by updateActionStates.
    if (m_act_run_layer) m_act_run_layer->setEnabled(caps.has_processing);
}

void MainWindow::onRemoveLayer(const std::string& layer_id)
{
    if (!m_project) return;
    const auto* layer = m_project->findLayer(layer_id);
    if (!layer) return;

    const QString name = QString::fromStdString(layer->label);
    if (QMessageBox::question(this, tr("Remove Layer"),
            tr("Remove \"%1\" from the project?\nThe source file will not be deleted.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    m_pending_sbp_builds.erase(layer_id);
    m_layer_nav_params.erase(layer_id);
    m_layer_wf_params.erase(layer_id);
    if (m_sss_ctrl) m_sss_ctrl->unloadLayer(layer_id);
    if (m_map_view) m_map_view->removeLayerData(layer_id);
    if (m_viewport_host) m_viewport_host->onLayerRemoved(layer_id);
    if (m_active_layer_id == layer_id) {
        m_active_layer_id.clear();
        if (m_inspector) m_inspector->showEmpty();
        updateControlsForModality(nullptr);
        updateActionStates();
        updateContextInfo();
    }
    m_project->removeLayer(layer_id);
    pruneNavigationHistory();
    refreshInspectorModalities();
    recordActivity(ActivityKind::GroupChange, tr("Removed: %1").arg(name));
}

void MainWindow::onRemoveLayers(const std::vector<std::string>& layer_ids)
{
    if (!m_project || layer_ids.empty()) return;

    const int n = static_cast<int>(layer_ids.size());
    if (QMessageBox::question(this, tr("Remove Layers"),
            tr("Remove %1 layer(s) from the project?\n"
               "Source files will not be deleted.").arg(n),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const bool active_removed = std::find(
        layer_ids.begin(), layer_ids.end(), m_active_layer_id) != layer_ids.end();

    for (const auto& id : layer_ids) {
        m_pending_sbp_builds.erase(id);
        m_layer_nav_params.erase(id);
        m_layer_wf_params.erase(id);
        if (m_sss_ctrl) m_sss_ctrl->unloadLayer(id);
        if (m_map_view) m_map_view->removeLayerData(id);
        if (m_viewport_host) m_viewport_host->onLayerRemoved(id);
        m_project->removeLayer(id);
    }
    if (active_removed) {
        m_active_layer_id.clear();
        if (m_inspector) m_inspector->showEmpty();
        updateControlsForModality(nullptr);
        updateActionStates();
        updateContextInfo();
    }
    pruneNavigationHistory();
    refreshInspectorModalities();
    recordActivity(ActivityKind::GroupChange,
        tr("Removed %1 layer(s)").arg(n));
}

void MainWindow::onNavigateBack()
{
    if (!m_project || m_navigation_index <= 0) {
        updateNavigationButtons();
        return;
    }

    --m_navigation_index;
    const std::string layer_id = m_navigation_history[static_cast<size_t>(m_navigation_index)];
    m_replaying_navigation = true;
    onLayerSelected(layer_id);
    m_replaying_navigation = false;
    updateNavigationButtons();
}

void MainWindow::onNavigateForward()
{
    if (!m_project
            || m_navigation_index < 0
            || m_navigation_index + 1 >= static_cast<int>(m_navigation_history.size())) {
        updateNavigationButtons();
        return;
    }

    ++m_navigation_index;
    const std::string layer_id = m_navigation_history[static_cast<size_t>(m_navigation_index)];
    m_replaying_navigation = true;
    onLayerSelected(layer_id);
    m_replaying_navigation = false;
    updateNavigationButtons();
}

void MainWindow::clearNavigationHistory()
{
    m_navigation_history.clear();
    m_navigation_index = -1;
    // Do not reset m_replaying_navigation here: clearNavigationHistory can be
    // called mid-replay (e.g. project close triggered from onLayerSelected).
    // The replay guard is owned by onNavigateBack/Forward and reset there.
    updateNavigationButtons();
}

void MainWindow::pruneNavigationHistory()
{
    if (!m_project) {
        clearNavigationHistory();
        return;
    }

    // Build a set of live IDs first so the inner lookup is O(1) not O(layers).
    std::unordered_set<std::string> live;
    for (const auto& l : m_project->layers())
        live.insert(l->id);

    std::vector<std::string> kept;
    kept.reserve(m_navigation_history.size());
    for (const auto& id : m_navigation_history) {
        if (live.count(id)) {
            if (kept.empty() || kept.back() != id)
                kept.push_back(id);
        }
    }

    m_navigation_history = std::move(kept);
    if (m_navigation_history.empty()) {
        m_navigation_index = -1;
    } else {
        m_navigation_index = std::clamp(
            m_navigation_index, 0,
            static_cast<int>(m_navigation_history.size()) - 1);
    }
    updateNavigationButtons();
}

static constexpr int kNavHistoryLimit = 100;

void MainWindow::recordNavigationSelection(const std::string& layer_id)
{
    if (!m_project || layer_id.empty() || !m_project->findLayer(layer_id))
        return;

    if (m_navigation_index >= 0
            && m_navigation_index < static_cast<int>(m_navigation_history.size())
            && m_navigation_history[static_cast<size_t>(m_navigation_index)] == layer_id)
        return;

    if (m_navigation_index + 1 < static_cast<int>(m_navigation_history.size())) {
        m_navigation_history.erase(
            m_navigation_history.begin() + m_navigation_index + 1,
            m_navigation_history.end());
    }

    m_navigation_history.push_back(layer_id);

    // Trim oldest entries if the cap is exceeded.
    if (static_cast<int>(m_navigation_history.size()) > kNavHistoryLimit) {
        const int excess = static_cast<int>(m_navigation_history.size()) - kNavHistoryLimit;
        m_navigation_history.erase(
            m_navigation_history.begin(),
            m_navigation_history.begin() + excess);
        m_navigation_index = std::max(0, m_navigation_index - excess);
    }

    m_navigation_index = static_cast<int>(m_navigation_history.size()) - 1;
    updateNavigationButtons();
}

void MainWindow::updateNavigationButtons()
{
    if (m_btn_nav_back)
        m_btn_nav_back->setEnabled(m_project && m_navigation_index > 0);
    if (m_btn_nav_forward)
        m_btn_nav_forward->setEnabled(
            m_project
            && m_navigation_index >= 0
            && m_navigation_index + 1 < static_cast<int>(m_navigation_history.size()));
}

void MainWindow::onRenameLayer(const std::string& layer_id)
{
    if (!m_project) return;
    auto* layer = m_project->findLayer(layer_id);
    if (!layer) return;

    bool ok = false;
    const QString current = QString::fromStdString(layer->label);
    const QString name = QInputDialog::getText(
        this, tr("Rename Layer"), tr("Name:"),
        QLineEdit::Normal, current, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == current) return;

    auto refresh = [this](const std::string& lid) {
        if (auto* l = m_project ? m_project->findLayer(lid) : nullptr) {
            if (m_line_list)    m_line_list->updateLayerLabel(lid, l->label);
            if (m_layer_picker) m_layer_picker->updateLayerLabel(lid, l->label);
            if (m_inspector && m_active_layer_id == lid) m_inspector->showLayer(l);
        }
        m_project_dirty = true;
        setWindowTitleFromProject();
        updateContextInfo();
    };

    const QString old_name = current;
    const QString new_name = name.trimmed();
    m_undo_stack->push(new RenameLayerCommand(
        m_project.get(),
        layer_id,
        layer->label,
        new_name.toStdString(),
        std::move(refresh)));
    recordActivity(ActivityKind::GroupChange,
        tr("Renamed: %1 → %2").arg(old_name, new_name));
}

void MainWindow::onRunLayers(const std::vector<std::string>& layer_ids)
{
    if constexpr (Features::kProcessing) {
        if (!m_project || !m_proc_ctrl) return;
        for (const auto& id : layer_ids) {
            auto* layer = m_project->findLayer(id);
            if (!layer) continue;
            const auto* src = m_project->findSource(layer->source_id);
            m_proc_ctrl->runLayer(layer, src ? src->path : std::string{});
        }
        if (!layer_ids.empty())
            recordActivity(ActivityKind::Processing,
                tr("Processing: %1 layer(s)").arg(static_cast<int>(layer_ids.size())));
    }
}

void MainWindow::onActivityPanel(int panel_id)
{
    togglePanel(panel_id, /*force_open=*/true);
}

void MainWindow::onRunAllLayers()
{
    if constexpr (Features::kProcessing) {
        if (m_proc_ctrl) {
            m_proc_ctrl->runAll();
            recordActivity(ActivityKind::Processing, tr("Processing: all layers"));
        }
    }
}

void MainWindow::onRunSelectedLayer()
{
    if constexpr (Features::kProcessing) {
        if (!m_project || m_active_layer_id.empty() || !m_proc_ctrl) return;
        auto* layer = m_project->findLayer(m_active_layer_id);
        if (!layer) return;
        auto* src = m_project->findSource(layer->source_id);
        m_proc_ctrl->runLayer(layer, src ? src->path : std::string{});
        recordActivity(ActivityKind::Processing,
            tr("Processing: %1").arg(QString::fromStdString(layer->label)));
    }
}

void MainWindow::refreshInspectorModalities()
{
    if (!m_inspector || !m_project) return;
    QSet<app::Modality> mods;
    for (const auto& l : m_project->layers())
        mods.insert(l->modality);
    m_inspector->setAvailableModalities(mods);
}

} // namespace dolphin::ui
