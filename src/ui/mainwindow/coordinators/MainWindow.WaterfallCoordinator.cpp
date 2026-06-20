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
#include "ui/shell/Features.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
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
#include "app/project/ProjectTransaction.h"
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

        connect(m_waterfall_win, &WaterfallWindow::cursorUpdated,
                this, &MainWindow::onWaterfallCursorUpdated);
        connect(m_waterfall_win, &WaterfallWindow::contactCreated,
                this, &MainWindow::onWaterfallContactCreated);
        connect(m_waterfall_win, &WaterfallWindow::paramsApplied,
                this, &MainWindow::onWaterfallParamsApplied);
        connect(m_waterfall_win, &WaterfallWindow::applyToAllRequested,
                this, &MainWindow::onWaterfallParamsApplied);
        connect(m_waterfall_win, &WaterfallWindow::setCrsRequested,
                this, &MainWindow::onWaterfallSetCrs);
        connect(m_waterfall_win, &WaterfallWindow::navProcessAllLinesRequested,
                this, &MainWindow::onWaterfallNavProcessAllLines);
        connect(m_waterfall_win, &WaterfallWindow::paletteChanged,
                this, [this](int idx) {
                    onPaletteChanged(idx);
                });
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
        // Nav / Geometry panels are wired once at construction (see
        // MainWindow.MainArea.cpp) so they work from the main/map view even when
        // this window is closed; they route to model-owned MainWindow slots. The
        // window's own analysis panel still persists via navProcessAllLinesRequested
        // (connected above). Only the waterfall-display panels stay window-coupled:
        // Gain / imaging panels push params back to the waterfall
        connect(m_gain_panel,    &GainControlPanel::applyToLineRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParams);
        connect(m_gain_panel,    &GainControlPanel::applyToAllRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParamsToAll);
        connect(m_imaging_panel, &ImagingControlPanel::applyToLineRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParams);
        connect(m_imaging_panel, &ImagingControlPanel::applyToAllRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParamsToAll);

        // When the waterfall applies any params, pull the latest state back into
        // the gain and imaging panels so they stay in sync with internal changes.
        // Also sync display params to the SSS map so its colours update globally.
        connect(m_waterfall_win, &WaterfallWindow::paramsApplied, this, [this]() {
            if (!m_waterfall_win) return;
            const auto& p = m_waterfall_win->currentParams();
            m_gain_panel->setParams(p);
            m_imaging_panel->setParams(p);
            if (m_sss_ctrl) {
                // Pass display params (palette, gain, contrast, threshold) to the
                // map but strip the auto-stretch values.  The waterfall's
                // display_low/high are calibrated to its in-session processed
                // amplitudes (post-TVG/ARC/AGC); the map renders DLPD amplitudes
                // which have a different numeric range.  Passing identity (0/1)
                // lets the map fall back to its own data-calibrated stretch.
                SonarDisplayParams map_dp = p;
                if (m_display_state)
                    map_dp.palette = m_display_state->mapPalette();
                map_dp.display_low  = 0.f;
                map_dp.display_high = 1.f;
                m_sss_ctrl->setDisplayParams(map_dp);
            }

            if (currentProject()) {
                // Use the layer the waterfall is actually showing, which may
                // differ from activeLayerId() when the user has navigated
                // Prev/Next inside the waterfall window.
                const std::string wf_id = m_waterfall_win->currentLayerId();
                if (!wf_id.empty()) {
                    auto* layer = currentProject()->findLayer(wf_id);
                    if (layer && m_display_state)
                        m_display_state->setLayerSssDisplay(wf_id, p);  // mutate + notify (marks dirty)
                    if (layer && layer->slant_range_corrected != p.slant_range_correction) {
                        layer->slant_range_corrected = p.slant_range_correction;
                        if (m_sss_ctrl) m_sss_ctrl->reloadLayer(wf_id);
                        app::ProjectTransaction tx(currentProject());
                        tx.commit();
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
            app::ProjectTransaction tx(currentProject());
            for (const auto& l : currentProject()->layers()) {
                if (!l) continue;
                l->slant_range_corrected = p.slant_range_correction;
                if (m_display_state) m_display_state->setLayerSssDisplay(l->id, p);
            }
            if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
            tx.commit();
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

    if (currentProject() && !activeLayerId().empty()) {
        auto* layer = currentProject()->findLayer(activeLayerId());
        if (layer && layer->modality == app::Modality::Sidescan) {
            const auto* src    = currentProject()->findSource(layer->source_id);
            const std::string path = src ? src->path : std::string{};
            const uint64_t    sz   = src ? src->size_bytes : 0;
            m_waterfall_win->setLayer(layer, m_import_service, path, sz);
            applyStoredNavParams(activeLayerId());
            m_waterfall_win->setProjectContacts(currentProject()->contacts());

            // Restore per-layer display params if the user has previously adjusted them.
            if (layer->sss_display_state.customized)
                m_waterfall_win->applyExternalParams(layer->sss_display_state.params);

            // Sync mini-panels to the waterfall's current params on initial open.
            // The SSS map is synced via the paramsApplied signal from applyExternalParams
            // above; if no stored params exist, m_display_params stays nullopt so the
            // map continues to use its own per-layer auto-stretch.
            if (m_gain_panel && m_imaging_panel) {
                const auto& p = m_waterfall_win->currentParams();
                m_gain_panel->setParams(p);
                m_imaging_panel->setParams(p);
            }
        }
    }

    // Sync palette from the display-state authority. The waterfall display is
    // global, so do not restore per-layer/inspector palette state on open.
    if (m_display_state)
        m_waterfall_win->setPalette(m_display_state->mapPalette());

    m_waterfall_win->show();
    m_waterfall_win->raise();
    m_waterfall_win->activateWindow();
}

void MainWindow::onWaterfallPrevLine(const std::string& from_layer_id)
{
    if (!currentProject()) return;
    const auto& layers = currentProject()->layers();
    if (layers.empty()) return;

    const std::string& ref_id = from_layer_id.empty() ? activeLayerId() : from_layer_id;

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i]->id == ref_id) { cur = i; break; }

    const int start = (cur >= 0) ? cur - 1 : static_cast<int>(layers.size()) - 1;
    for (int i = start; i >= 0; --i) {
        if (layers[i]->index_built && layers[i]->artifactCount() > 0) {
            onLayerSelected(layers[i]->id);
            return;
        }
    }
    appendJobMessage("Already on the first survey line.");
}

void MainWindow::onWaterfallNextLine(const std::string& from_layer_id)
{
    if (!currentProject()) return;
    const auto& layers = currentProject()->layers();
    if (layers.empty()) return;

    const std::string& ref_id = from_layer_id.empty() ? activeLayerId() : from_layer_id;

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i]->id == ref_id) { cur = i; break; }

    const int start = cur + 1;
    for (int i = start; i < static_cast<int>(layers.size()); ++i) {
        if (layers[i]->index_built && layers[i]->artifactCount() > 0) {
            onLayerSelected(layers[i]->id);
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
        win->setProject(currentProject(), m_import_service, activeLayerId());

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
