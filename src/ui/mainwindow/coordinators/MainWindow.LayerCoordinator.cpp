// MainWindow.LayerCoordinator.cpp — layer selection and activity-panel coordination.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/Features.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/track/TrackMapBuild.h"
#include "ui/features/processing/ProcessingController.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/widgets/PanelTabBar.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "app/project/Project.h"
#include "app/layers/CapabilitySet.h"
#include "app/layers/DataLayer.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>
#include <QStackedWidget>
#include <QToolButton>
#include <QUndoStack>

#include <algorithm>

namespace dolphin::ui {

void MainWindow::onLayerSelected(const std::string& layer_id)
{
    if (!currentProject()) return;
    if (!m_layer_ctrl->isReplaying())
        m_layer_ctrl->recordSelection(layer_id);

    m_layer_ctrl->setActiveLayer(layer_id);

    auto* layer = currentProject()->findLayer(layer_id);

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

    if (m_viewport_host)
        m_viewport_host->setActiveLayer(layer_id);
    else if (m_map_view)
        m_map_view->setActiveLayer(layer_id);

    if (layer) {
        using M = app::Modality;
        const M mod = layer->modality;

        // Sidescan: full background swath build via the SSS controller.
        if (m_sss_ctrl && mod == M::Sidescan)
            m_sss_ctrl->activateLayer(layer_id, currentProject());

        // MAG / MBE: rebuildNavTrack (called by setActiveLayer) builds the track.
        else if (m_map_view && (mod == M::Magnetometer || mod == M::Multibeam))
            m_map_view->setNavTrackVisible(layer_id, true);

        // Raster: depth -> 3D terrain mesh (+ 2D overlay); visual -> 2D overlay.
        else if (mod == M::Raster)
            displayRaster(layer);

        // SubBottom: loads full trace data then builds a Profile LayerMapData
        // (nav track + per-trace bottom-depth scalar for the colored map ribbon).
        // Skipped if real map data (non-empty nav_track) is already present for
        // this layer — avoids redundant disk reads on re-selection.
        else if (m_map_view && mod == M::SubBottom) {
            // Skip the rebuild if real map data is already present — avoids
            // redundant disk reads on re-selection. buildSbpProfileMap applies
            // any stored nav corrections for the layer.
            if (layer->index_built && !layer->artifact_index.empty()) {
                const auto* existing = m_map_view->layerData(layer_id);
                if (!existing || existing->nav_track.empty())
                    buildSbpProfileMap(layer);
            }
        }
    }

    if (m_inspector)
        layer ? m_inspector->showLayer(layer) : m_inspector->showEmpty();

    // Feed the modal host (lower panel) for modality-specific tools.
    if (m_modal_host)
        layer ? m_modal_host->setLayer(layer) : m_modal_host->clearLayer();

    // Auto-switch the sensor tab to match the selected layer's modality.
    if (layer) {
        using M = app::Modality;
        auto tab = layer->modality;
        if (tab != M::Sidescan && tab != M::SubBottom && tab != M::Magnetometer)
            tab = M::Unknown;  // Multibeam, Raster, etc. → no tab, universal sections only
        refreshSensorTab(tab);
    }
    updateToolsApplyBar();   // show the shared Apply bar for SSS/SBP layers only
    refreshViewsPanel(true); // left Views panel follows the active layer's modality

    // Always bring the Properties tab into view when a layer is selected.
    if (layer && m_props_stack && m_props_stack->currentIndex() != 0) {
        m_props_stack->setCurrentIndex(0);
        if (m_props_tab_tools) m_props_tab_tools->setChecked(true);
    }

    // Re-fit the upper pane to the freshly populated inspector content.
    adjustPropsSplit();


    if (m_waterfall_win && m_waterfall_win->isVisible()) {
        if (layer && m_waterfall_win->currentLayerId() != layer->id) {
            const auto* src = currentProject()->findSource(layer->source_id);
            const std::string path = src ? src->path : std::string{};
            const uint64_t    sz   = src ? src->size_bytes : 0;
            m_waterfall_win->setLayer(layer, path, sz);
            applyStoredNavParams(layer->id);
            if (layer->sss_display_state.customized)
                m_waterfall_win->applyExternalParams(layer->sss_display_state.params);
            // Waterfall display palette is global; keep the current display-state
            // palette when switching lines instead of restoring per-layer UI state.
            if (m_display_state)
                m_waterfall_win->setPalette(m_display_state->mapPalette());
            // Restore per-layer channel selection.
            m_waterfall_win->setDisplayChannel(
                layer->sss_display_state.params.display_channel);
        } else if (!layer) {
            m_waterfall_win->clearLayer();
        }
    }

    if (m_sbp_win && m_sbp_win->isVisible()) {
        if (layer && layer->modality == app::Modality::SubBottom) {
            if (m_sbp_win->currentLayerId() != layer->id) {
                const auto* src = currentProject()->findSource(layer->source_id);
                const std::string path = src ? src->path : std::string{};
                const uint64_t    sz   = src ? src->size_bytes : 0;
                m_sbp_win->setLayer(layer, path, sz);
                // Restore per-layer SBP display params; palette always wins if set.
                if (layer->sbp_display_state.display_customized)
                    m_sbp_win->applyDisplayParams(layer->sbp_display_state.display);
                if (layer->sbp_palette >= 0)
                    m_sbp_win->setPalette(layer->sbp_palette);
                if (layer->sbp_display_state.gain_customized)
                    m_sbp_win->applyGainParams(layer->sbp_display_state.gain);
                if (layer->sbp_display_state.signal_customized)
                    m_sbp_win->applySignalParams(layer->sbp_display_state.signal);
                applyStoredSbpNavParams(layer->id);  // stored nav corrections
                if (m_modal_host) {
                    if (auto* gm = m_modal_host->sbpGainModule(); layer->sbp_display_state.gain_customized)
                        gm->setParams(layer->sbp_display_state.gain);
                    if (auto* sm = m_modal_host->sbpSignalModule(); layer->sbp_display_state.signal_customized)
                        sm->setParams(layer->sbp_display_state.signal);
                }
            }
        } else {
            m_sbp_win->clearLayer();
        }
    }

    if constexpr (Features::kNodeGraph) {
        if (m_node_graph_win && m_node_graph_win->isVisible())
            m_node_graph_win->setLayer(layer, currentProject());
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
    if (!currentProject()) return;
    const auto* layer = currentProject()->findLayer(layer_id);
    if (!layer) return;

    const QString name = QString::fromStdString(layer->label);
    if (QMessageBox::question(this, tr("Remove Layer"),
            tr("Remove \"%1\" from the project?\n\n"
               "The original source file is kept. Durable parsed data (.dlpd/.dpcache) "
               "is retained until the project itself is deleted.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (m_op_mgr) m_op_mgr->cancelByKey("sbp_profile:" + layer_id);
    if (m_sss_ctrl) m_sss_ctrl->unloadLayer(layer_id);
    if (m_viewport_host) m_viewport_host->onLayerRemoved(layer_id);
    else if (m_map_view) m_map_view->removeLayerData(layer_id);
    if (activeLayerId() == layer_id) {
        m_layer_ctrl->clearActiveLayer();
        if (m_inspector)    m_inspector->showEmpty();
        if (m_modal_host)   m_modal_host->clearLayer();
        updateControlsForModality(nullptr);
        updateActionStates();
        updateContextInfo();
    }
    currentProject()->removeLayer(layer_id);
    m_layer_ctrl->pruneHistory(currentProject());
    refreshInspectorModalities();
    recordActivity(ActivityKind::GroupChange, tr("Removed: %1").arg(name));
}

void MainWindow::onRemoveLayers(const std::vector<std::string>& layer_ids)
{
    if (!currentProject() || layer_ids.empty()) return;

    const int n = static_cast<int>(layer_ids.size());
    if (QMessageBox::question(this, tr("Remove Layers"),
            tr("Remove %1 layer(s) from the project?\n\n"
               "Original source files are kept. Durable parsed data (.dlpd/.dpcache) "
               "is retained until the project itself is deleted.").arg(n),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const bool active_removed = std::find(
        layer_ids.begin(), layer_ids.end(), activeLayerId()) != layer_ids.end();

    for (const auto& id : layer_ids) {
        if (m_op_mgr) m_op_mgr->cancelByKey("sbp_profile:" + id);
        if (m_sss_ctrl) m_sss_ctrl->unloadLayer(id);
        if (m_viewport_host) m_viewport_host->onLayerRemoved(id);
        else if (m_map_view) m_map_view->removeLayerData(id);
        currentProject()->removeLayer(id);
    }
    if (active_removed) {
        m_layer_ctrl->clearActiveLayer();
        if (m_inspector)    m_inspector->showEmpty();
        if (m_modal_host)   m_modal_host->clearLayer();
        updateControlsForModality(nullptr);
        updateActionStates();
        updateContextInfo();
    }
    m_layer_ctrl->pruneHistory(currentProject());
    refreshInspectorModalities();
    recordActivity(ActivityKind::GroupChange,
        tr("Removed %1 layer(s)").arg(n));
}

void MainWindow::onRenameLayer(const std::string& layer_id)
{
    if (!currentProject()) return;
    auto* layer = currentProject()->findLayer(layer_id);
    if (!layer) return;

    bool ok = false;
    const QString current = QString::fromStdString(layer->label);
    const QString name = QInputDialog::getText(
        this, tr("Rename Layer"), tr("Name:"),
        QLineEdit::Normal, current, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == current) return;

    auto refresh = [this](const std::string& lid) {
        if (auto* l = currentProject() ? currentProject()->findLayer(lid) : nullptr) {
            if (m_line_list)    m_line_list->updateLayerLabel(lid, l->label);
            if (m_inspector && activeLayerId() == lid) m_inspector->showLayer(l);
        }
        
        updateContextInfo();
    };

    const QString old_name = current;
    const QString new_name = name.trimmed();
    m_undo_stack->push(new RenameLayerCommand(
        currentProject(),
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
        if (!currentProject() || !m_proc_ctrl) return;
        for (const auto& id : layer_ids) {
            auto* layer = currentProject()->findLayer(id);
            if (!layer) continue;
            const auto* src = currentProject()->findSource(layer->source_id);
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
        if (!currentProject() || activeLayerId().empty() || !m_proc_ctrl) return;
        auto* layer = currentProject()->findLayer(activeLayerId());
        if (!layer) return;
        auto* src = currentProject()->findSource(layer->source_id);
        m_proc_ctrl->runLayer(layer, src ? src->path : std::string{});
        recordActivity(ActivityKind::Processing,
            tr("Processing: %1").arg(QString::fromStdString(layer->label)));
    }
}

void MainWindow::refreshSensorTab(app::Modality m)
{
    using M = app::Modality;
    // Uncheck-all first: the bar's group is exclusive, so plain setChecked(false)
    // cannot clear a checked tab. Unknown (no sensor) leaves no tab checked —
    // the modal host then shows only the universal sections.
    if (m_sensor_bar) m_sensor_bar->clearSelection();
    if      (m == M::Sidescan     && m_tab_sss) m_tab_sss->setChecked(true);
    else if (m == M::SubBottom    && m_tab_sbp) m_tab_sbp->setChecked(true);
    else if (m == M::Magnetometer && m_tab_mag) m_tab_mag->setChecked(true);

    if (m_modal_host)
        m_modal_host->setModalityFilter(m);
}

void MainWindow::refreshInspectorModalities()
{
    if (!m_inspector) return;

    using M = app::Modality;
    QSet<M> mods;
    if (currentProject()) {
        for (const auto& l : currentProject()->layers())
            mods.insert(l->modality);
    }

    const bool has_sss = mods.contains(M::Sidescan);
    const bool has_sbp = mods.contains(M::SubBottom);
    const bool has_mag = mods.contains(M::Magnetometer);

    // Show a sensor tab only when the project actually contains that modality.
    // No point dangling unusable tabs — a project without sensor data hides the
    // whole strip (the universal sections below stay). A visible tab must also
    // be enabled (the tabs start disabled at construction) or it cannot take
    // the checked state.
    auto setTab = [](QToolButton* b, bool on) { if (b) { b->setVisible(on); b->setEnabled(on); } };
    setTab(m_tab_sss, has_sss);
    setTab(m_tab_sbp, has_sbp);
    setTab(m_tab_mag, has_mag);
    if (m_sensor_bar) m_sensor_bar->setVisible(has_sss || has_sbp || has_mag);
    // With the universal sections gone, the whole lower shell is empty when no
    // sensor modality exists — hide it rather than leaving a bare header strip.
    if (m_sensor_shell) m_sensor_shell->setVisible(has_sss || has_sbp || has_mag);

    // The tab that should be active for the current selection: the active layer's
    // sensor when it has one, else none (universal sections only).
    M want = M::Unknown;
    if (currentProject() && !activeLayerId().empty()) {
        if (const auto* l = currentProject()->findLayer(activeLayerId())) {
            const M lm = l->modality;
            if      (lm == M::Sidescan     && has_sss) want = M::Sidescan;
            else if (lm == M::SubBottom    && has_sbp) want = M::SubBottom;
            else if (lm == M::Magnetometer && has_mag) want = M::Magnetometer;
        }
    }

    // Which tab is currently checked?
    M cur = M::Unknown;
    if      (m_tab_sss && m_tab_sss->isChecked()) cur = M::Sidescan;
    else if (m_tab_sbp && m_tab_sbp->isChecked()) cur = M::SubBottom;
    else if (m_tab_mag && m_tab_mag->isChecked()) cur = M::Magnetometer;

    const bool cur_visible =
        (cur == M::Sidescan     && has_sss) ||
        (cur == M::SubBottom    && has_sbp) ||
        (cur == M::Magnetometer && has_mag) ||
        (cur == M::Unknown);   // no selection is always a valid state

    // Retarget when the current tab is no longer usable, or when nothing is
    // selected while a sensor layer is active. A still-valid manual sensor
    // choice is left be.
    if (!cur_visible || (cur == M::Unknown && want != M::Unknown))
        refreshSensorTab(want);

    // The shared bottom Apply bar is only relevant for sensor (SSS/SBP) layers.
    updateToolsApplyBar();
}

} // namespace dolphin::ui
