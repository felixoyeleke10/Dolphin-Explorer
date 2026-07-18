// MainWindow.WaterfallCoordinator.cpp — waterfall window lifecycle and state reflection.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/ViewerWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"
#include "ui/shell/Features.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/shared/LineNavigation.h"
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/features/waterfall/WaterfallSettingsDialog.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/contacts/ContactManagerWindow.h"
#include "ui/systems/ProjectEventBus.h"
#include "ui/features/map/MapView.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"

#include <QMessageBox>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

void MainWindow::onWaterfallOpen()
{
    if (!m_waterfall_win) {
        m_waterfall_win = new WaterfallWindow(m_app_state, nullptr);
        m_waterfall_win->setOperationManager(m_op_mgr);  // owns pipeline ops (keyed)
        m_window_registry->registerViewer(m_waterfall_win, m_waterfall_win);
        connect(m_waterfall_win, &WaterfallWindow::newFileRequested,
                this, &MainWindow::onImportFile);
        connect(m_waterfall_win, &WaterfallWindow::openFileRequested,
                this, &MainWindow::onOpenProject);
        connect(m_waterfall_win, &WaterfallWindow::saveFileRequested,
                this, &MainWindow::onSaveProject);
        connect(m_waterfall_win, &WaterfallWindow::propertiesRequested,
                this, &MainWindow::toggleProperties);
        connect(m_waterfall_win, &WaterfallWindow::metadataRequested,
                this, &MainWindow::onWaterfallMetadata);
        connect(m_waterfall_win, &WaterfallWindow::contactManagerRequested,
                this, &MainWindow::onContactManagerOpen);
        connect(m_waterfall_win, &WaterfallWindow::settingsRequested,
                this, &MainWindow::onWaterfallSettings);
        connect(m_waterfall_win, &WaterfallWindow::prevLineRequested,
                this, &MainWindow::onWaterfallPrevLine);
        connect(m_waterfall_win, &WaterfallWindow::nextLineRequested,
                this, &MainWindow::onWaterfallNextLine);
        // Palette picked inside the waterfall is a change of the ONE global SSS
        // palette: route it through onPaletteChanged → DisplayStateManager, the
        // same authority the right panel and Views use, so the map mosaic and
        // every other surface follow. (Round-trip is loop-safe: the bus handler
        // pushes back via setPalette, which no-ops on an unchanged index.)
        connect(m_waterfall_win, &WaterfallWindow::paletteChanged,
                this, &MainWindow::onPaletteChanged);

        connect(m_waterfall_win, &WaterfallWindow::cursorUpdated,
                this, &MainWindow::onWaterfallCursorUpdated);
        connect(m_waterfall_win, &WaterfallWindow::contactCreated,
                this, &MainWindow::onWaterfallContactCreated);
        connect(m_waterfall_win, &WaterfallWindow::featureCreated,
                this, &MainWindow::onWaterfallFeatureCreated);
        connect(m_waterfall_win, &WaterfallWindow::clearAllContactsRequested,
                this, &MainWindow::onClearContacts);
        connect(m_waterfall_win, &WaterfallWindow::contactEditRequested,
                this, &MainWindow::onContactEditRequested);
        connect(m_waterfall_win, &WaterfallWindow::paramsApplied,
                this, &MainWindow::onWaterfallParamsApplied);
        connect(m_waterfall_win, &WaterfallWindow::applyToAllRequested,
                this, &MainWindow::onWaterfallParamsApplied);
        connect(m_waterfall_win, &WaterfallWindow::setCrsRequested,
                this, &MainWindow::onWaterfallSetCrs);
        connect(m_waterfall_win, &WaterfallWindow::qcViewedFractionChanged,
                this, [this](const std::string& /*layer_id*/, float /*fraction*/) {
                    // WaterfallScrollSync already writes to layer->qc_viewed_fraction
                    // before emitting this signal — just mark the project dirty so
                    // the updated fraction is included in the next save.
                    if (currentProject() && !isProjectDirty())
                        markProjectDirty();
                });
        // Busy-state → status-bar spinner. Cancellation is owned by
        // OperationManager (the window's pipeline ops are keyed), so no
        // external-token registration is needed.
        connect(m_waterfall_win, &WaterfallWindow::dataStateChanged,
                this, [this](ViewerDataState) { refreshLoadingIndicator(); });
        connect(m_waterfall_win, &WaterfallWindow::layerChangeRequested,
                this, [this](const std::string& id) { onLayerSelected(id); });

        // -- Control panel wiring ------------------------------------------
        // Nav / Geometry AND Gain / Imaging panels are all wired once at
        // construction (see MainWindow.MainArea.cpp) so they work from the
        // main/map view even when this window is closed; they route to model-owned
        // MainWindow slots. The gain/imaging slots (onSssDisplayApply*) route back
        // through this window's applyExternalParams when it is open, so do NOT
        // connect them here too — that would apply every change twice.

        // When the waterfall applies any params, pull the latest state back into
        // the gain and imaging panels so they stay in sync with internal changes.
        // Also sync display params to the SSS map so its colours update globally.
        connect(m_waterfall_win, &WaterfallWindow::paramsApplied, this, [this]() {
            if (!m_waterfall_win) return;
            const auto& p = m_waterfall_win->currentParams();
            // Gain/imaging are per-layer. The map rebuild below reads this
            // layer's stored params; never apply one waterfall line globally.

            if (currentProject()) {
                // Use the layer the waterfall is actually showing, which may
                // differ from activeLayerId() when the user has navigated
                // Prev/Next inside the waterfall window.
                const std::string wf_id = m_waterfall_win->currentLayerId();
                if (!wf_id.empty()) {
                    auto* layer = currentProject()->findLayer(wf_id);
                    const bool src_changed = layer
                        && layer->slant_range_corrected != p.slant_range_correction;
                    if (layer && m_display_state) {
                        // Snapshot old pipeline params so we can skip the raster rebuild
                        // when paramsApplied fires from a programmatic restore (layer
                        // switch, waterfall open) rather than a user Apply click.
                        const WaterfallParams old_p = layer->sss_display_state.params;
                        if (m_sss_processing)
                            m_sss_processing->commit(currentProject(), {wf_id}, p);
                        // Rebuild the map raster so corrections (destripe, AGC, ARC, TVG,
                        // etc.) applied in the waterfall are immediately reflected in the
                        // map mosaic.  Only rebuild when pipeline params actually changed
                        // (avoids a wasteful rebuild on every layer switch/waterfall open).
                        // Skip when SRC is changing — reloadLayer below does a full reload.
                        const bool pipeline_changed =
                               p.destripe     != old_p.destripe
                            || p.agc          != old_p.agc
                            || p.tvg          != old_p.tvg
                            || p.arn          != old_p.arn
                            || p.arc          != old_p.arc
                            || p.beam_pattern != old_p.beam_pattern
                            || p.ml_enhance   != old_p.ml_enhance;
                        if (m_sss_ctrl
                                && !src_changed
                                && pipeline_changed)
                            m_sss_ctrl->applyLiveCorrections({wf_id});
                    }
                    if (layer && src_changed) {
                        if (m_sss_ctrl) m_sss_ctrl->reloadLayer(wf_id);
                        markProjectDirty();
                        m_session_ctrl->autoSave();
                        // If the viewer holds detected bottom picks, persist them to
                        // the DLPD so the map georeferencer can use them.  The async
                        // bake will trigger a second SSS reload with the correct data.
                        if (m_corr_op) {
                            auto viewer_pings = m_waterfall_win->currentRawPings();
                            const bool has_picks = std::any_of(
                                viewer_pings.begin(), viewer_pings.end(),
                                [](const core::SidescanPing& vp) {
                                    return vp.bottom_pick.source > 0
                                        && vp.bottom_pick.range_m > 0.f;
                                });
                            if (has_picks) {
                                const auto* src2 = currentProject()->findSource(layer->source_id);
                                // Persist ONLY the bottom picks (georef data the map
                                // needs after an SRC toggle) — pass empty correction
                                // params so this does NOT bake TVG/ARC/AGC. Amplitude
                                // corrections stay live display state; committing them
                                // is the explicit "Bake Corrections" command.
                                m_corr_op->applySSS(layer,
                                                    src2 ? src2->path : std::string{},
                                                    app::SidescanCorrectionParams{},
                                                    std::move(viewer_pings));
                            }
                        }
                    }
                }
            }
        });

        // "Apply to all" — propagate full params + SRC to every layer.
        connect(m_waterfall_win, &WaterfallWindow::applyToAllRequested, this, [this]() {
            if (!currentProject() || !m_waterfall_win) return;
            const WaterfallParams p = m_waterfall_win->currentParams();
            if (m_gain_panel)    m_gain_panel->setParams(p);
            if (m_imaging_panel) m_imaging_panel->setParams(p);
            const auto ids = SidescanProcessingCoordinator::allSidescanLayerIds(
                currentProject());
            if (m_sss_processing)
                m_sss_processing->commit(currentProject(), ids, p);
            if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
            markProjectDirty();
            m_session_ctrl->autoSave();
            // Display-state only — no .dlpd bake here. The waterfall renders the
            // corrections live and the map shows gain/contrast live; committing the
            // full corrections (incl. TVG/AGC/ARC) into .dlpd for the map mosaic and
            // exports is the explicit Processing → "Bake Corrections into Data" command.
        });

        // Gain/imaging Apply (Line/All) is display-state only: the panels already
        // push params to the waterfall live (applyExternalParams above) and the map
        // shows gain/contrast live (paramsApplied → setDisplayParams). No .dlpd write
        // happens on Apply. Committing the full corrections into .dlpd (for the map
        // mosaic and exports) is the explicit Processing → "Bake Corrections into
        // Data" command (onBakeCorrections).
    }

    // Populate the FILES list with every sidescan layer in the current project.
    if (currentProject()) {
        std::vector<std::pair<std::string, std::string>> sss_layers;
        for (const auto& l : currentProject()->layers())
            if (l && l->modality == app::Modality::Sidescan)
                sss_layers.emplace_back(l->id, l->label);
        m_waterfall_win->setProjectLayers(sss_layers);
    }

    // Resolve which sidescan line to show: the active layer if it is sidescan, else
    // the first indexed sidescan line. Opening the viewer while a non-SSS layer is
    // active must still land on a real SSS line.
    std::string sss_id;
    if (currentProject()) {
        if (auto* al = currentProject()->findLayer(activeLayerId());
            al && al->modality == app::Modality::Sidescan)
            sss_id = activeLayerId();
        else
            for (const auto& l : currentProject()->layers())
                if (l && l->modality == app::Modality::Sidescan
                        && l->index_built && l->sidescanCount() > 0) {
                    sss_id = l->id;
                    break;
                }
    }

    if (!sss_id.empty()) {
        if (sss_id != activeLayerId()) onLayerSelected(sss_id);  // sync app/map/inspector
        auto* layer = currentProject()->findLayer(sss_id);
        if (layer && layer->modality == app::Modality::Sidescan) {
            const auto* src    = currentProject()->findSource(layer->source_id);
            const std::string path = src ? src->path : std::string{};
            const uint64_t    sz   = src ? src->size_bytes : 0;
            m_waterfall_win->setLayer(layer, path, sz);
            applyStoredNavParams(sss_id);
            m_waterfall_win->setProjectContacts(currentProject()->contacts());

        }
    }

    // Sync palette from the display-state authority. The waterfall display is
    // global, so do not restore per-layer/inspector palette state on open.

    // Reflect Prev/Next availability for the VIEWER's current line (source of truth).
    {
        // Match the FILES list (populated by modality) so the buttons reflect the SSS
        // lines the user sees — not only those whose index is currently loaded.
        const auto nav = computeLineNav(currentProject(), m_waterfall_win->currentLayerId(),
            [](const app::DataLayer& l) { return l.modality == app::Modality::Sidescan; });
        m_waterfall_win->setLineNavEnabled(nav.has_prev, nav.has_next);
    }

    m_waterfall_win->show();
    m_waterfall_win->raise();
    m_waterfall_win->activateWindow();
}

void MainWindow::onWaterfallPrevLine(const std::string& from_layer_id)
{
    if (!currentProject() || !m_waterfall_win) return;
    const auto& layers = currentProject()->layers();
    if (layers.empty()) return;

    // The waterfall's own loaded line is the source of truth (not the app active layer).
    const std::string ref_id = !from_layer_id.empty() ? from_layer_id
                                                       : m_waterfall_win->currentLayerId();

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i]->id == ref_id) { cur = i; break; }

    for (int i = cur - 1; i >= 0; --i) {
        // Stay within sidescan lines — the waterfall only shows SSS.
        if (layers[i] && layers[i]->modality == app::Modality::Sidescan) {
            onLayerSelected(layers[i]->id);
            onWaterfallOpen();   // reload the open waterfall to the new line
            return;
        }
    }
    appendJobMessage("Already on the first survey line.");
}

void MainWindow::onWaterfallNextLine(const std::string& from_layer_id)
{
    if (!currentProject() || !m_waterfall_win) return;
    const auto& layers = currentProject()->layers();
    if (layers.empty()) return;

    const std::string ref_id = !from_layer_id.empty() ? from_layer_id
                                                       : m_waterfall_win->currentLayerId();

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i]->id == ref_id) { cur = i; break; }

    const int start = cur + 1;
    for (int i = start; i < static_cast<int>(layers.size()); ++i) {
        // Stay within sidescan lines — the waterfall only shows SSS.
        if (layers[i] && layers[i]->modality == app::Modality::Sidescan) {
            onLayerSelected(layers[i]->id);
            onWaterfallOpen();   // reload the open waterfall to the new line
            return;
        }
    }
    appendJobMessage("Already on the last survey line.");
}

void MainWindow::onWaterfallCursorUpdated(float range_m, const QString& /*side*/,
                                          double lat, double lon, bool is_projected)
{
    if (!m_status_bar) return;
    // Only the live coordinate is shown in the status bar; the sidescan range readout
    // was removed (it floated over the project name).
    if (range_m <= 0.f) {
        m_status_bar->clearCursorPosition();
        return;
    }

    if (lat != 0.0 || lon != 0.0)
        showCursorPosition(lat, lon, is_projected);
    else
        m_status_bar->clearCursorPosition();
}

void MainWindow::onWaterfallParamsApplied()
{
    if (!activeLayerId().empty() && currentProject()) {
        if (const auto* layer = currentProject()->findLayer(activeLayerId())) {
            recordActivity(ActivityKind::DisplayParams,
                tr("Display params applied to %1")
                    .arg(QString::fromStdString(layer->label)));
        }
    } else {
        recordActivity(ActivityKind::DisplayParams, tr("Display parameters applied"));
    }
}

void MainWindow::onWaterfallMetadata()
{
    if (!m_metadata_win) {
        auto* win = new SSSMetadataWindow(nullptr);
        win->setAttribute(Qt::WA_DeleteOnClose);
        m_metadata_win = win;
    }

    auto* win = qobject_cast<SSSMetadataWindow*>(m_metadata_win);
    if (win)
        win->setProject(currentProject(), activeLayerId());

    m_metadata_win->show();
    m_metadata_win->raise();
    m_metadata_win->activateWindow();
}

void MainWindow::onWaterfallSettings()
{
    if (!m_waterfall_win) return;
    auto* dlg = new WaterfallSettingsDialog(
        m_waterfall_win->wfSettings(),
        static_cast<QWidget*>(m_waterfall_win));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &WaterfallSettingsDialog::applied,
            this, [this](WaterfallSettingsDialog::Settings s) {
                if (m_waterfall_win) m_waterfall_win->applyWfSettings(s);
            });
    dlg->show();
}

} // namespace dolphin::ui
