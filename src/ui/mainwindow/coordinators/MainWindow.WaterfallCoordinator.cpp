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
#include "ui/features/map/MapView.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/project/Project.h"
#include "app/project/ProjectTransaction.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"

#include <QMessageBox>

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
                    onPaletteChanged(idx);   // global map palette apply (unchanged)
                    // Per-layer SSS palette override → through the display-state
                    // manager: it writes the layer field and emits
                    // displayStateChanged(layer, Palette); MainWindow marks the project
                    // dirty on that bus signal, so it persists and other views can react.
                    if (m_display_state && !activeLayerId().empty())
                        m_display_state->setLayerSssPalette(activeLayerId(), idx);
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
                    if (layer) {
                        layer->sss_display_state.params = p;
                        layer->sss_display_state.customized = true;
                        markProjectDirty();
                    }
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
                l->sss_display_state.params = p;
                l->sss_display_state.customized = true;
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

    // Sync palette: use the per-layer saved palette if available, otherwise fall
    // back to the Properties inspector (which shows the app-wide default).
    {
        auto* layer = currentProject() ? currentProject()->findLayer(activeLayerId()) : nullptr;
        if (layer && layer->sss_palette >= 0)
            m_waterfall_win->setPalette(layer->sss_palette);
        else if (m_inspector)
            m_waterfall_win->setPalette(m_inspector->currentPaletteIndex());
    }

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

void MainWindow::onWaterfallContactCreated(float range_m, double lat, double lon,
                                           bool is_projected,
                                           const QString& classification,
                                           const QString& line_id,
                                           uint64_t abs_row,
                                           int channel_idx)
{
    if (!currentProject()) return;

    const int n = static_cast<int>(currentProject()->contacts().size()) + 1;
    core::Contact c;
    c.label          = QString("C%1").arg(n, 3, 10, QChar('0')).toStdString();
    c.lat            = lat;
    c.lon            = lon;
    c.spatial_ref    = is_projected
                     ? core::makeUnknownProjectedSpatialRef()
                     : core::makeWgs84SpatialRef();
    c.classification = classification.toStdString();
    c.line_id        = line_id.toStdString();
    c.range_m        = range_m;
    c.artifact_id    = abs_row;
    c.sample_idx     = static_cast<uint32_t>(channel_idx);

    m_undo_stack->push(new AddContactCommand(
        currentProject(), c,
        [this]() {  }));

    appendJobMessage(
        QString("Contact %1 placed \u2014 %2 (%3 m)")
            .arg(QString::fromStdString(c.label))
            .arg(classification)
            .arg(range_m, 0, 'f', 1));
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

void MainWindow::onContactSelected(uint64_t contact_id)
{
    if (m_map_view) m_map_view->setSelectedContact(contact_id);

    if (!currentProject() || !m_inspector) return;
    for (const auto& c : currentProject()->contacts())
        if (c.id == contact_id) { m_inspector->showContact(&c); return; }
}

void MainWindow::onContactPicked(double lat, double lon,
                                 uint64_t /*artifact_id*/, uint32_t /*sample_idx*/)
{
    appendJobMessage(QString("Picked  Lat %1  Lon %2")
        .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
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

void MainWindow::onWaterfallSetCrs(const std::string& from_layer_id)
{
    if (!currentProject()) return;

    // Use the layer the waterfall is actually showing, which may differ from
    // activeLayerId() when the user has used Prev/Next inside the waterfall.
    const std::string ref_id = from_layer_id.empty() ? activeLayerId() : from_layer_id;
    auto* layer = currentProject()->findLayer(ref_id);
    if (!layer) return;

    // Open picker pre-seeded with the current (possibly unconfirmed) CRS
    QWidget* parent = m_waterfall_win ? static_cast<QWidget*>(m_waterfall_win) : this;
    CrsPickerDialog dlg(layer->source_spatial_ref, parent);
    if (dlg.exec() != QDialog::Accepted) return;

    const core::SpatialRef new_ref = dlg.selectedRef();
    if (new_ref.kind == core::SpatialRefKind::Unknown) return;

    const core::SpatialRef old_ref = layer->source_spatial_ref;

    // Source CRS is a source-file property — apply to every layer and source in
    // the project.  The project display CRS (map target) is left unchanged; the
    // normalisation step reprojects source coordinates into it on reload.
    auto apply_crs = [this, ref_id](const core::SpatialRef& ref) {
        if (!currentProject()) return;
        {
            app::ProjectTransaction tx(currentProject());
            for (const auto& l : currentProject()->layers())
                if (l) l->source_spatial_ref = ref;
            for (const auto& l : currentProject()->layers()) {
                if (!l) continue;
                if (auto* src = currentProject()->findSource(l->source_id))
                    src->source_spatial_ref = ref;
            }
            tx.commit();
        }
        auto* lyr = currentProject()->findLayer(ref_id);
        if (!ref_id.empty() && ref_id != activeLayerId()) {
            m_layer_ctrl->setActiveLayer(ref_id);
            m_app_state->setSelection({ref_id, lyr ? lyr->modality : app::Modality::Unknown});
            if (m_inspector && lyr) m_inspector->showLayer(lyr);
        }
        if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
        if (m_waterfall_win && lyr) {
            const auto* src = currentProject()->findSource(lyr->source_id);
            m_waterfall_win->setLayer(lyr, m_import_service,
                                      src ? src->path : std::string{},
                                      src ? src->size_bytes : 0);
            applyStoredNavParams(lyr->id);
            if (lyr->sss_display_state.customized)
                m_waterfall_win->applyExternalParams(lyr->sss_display_state.params);
            // Restore per-layer palette after CRS-triggered reload.
            if (lyr->sss_palette >= 0)
                m_waterfall_win->setPalette(lyr->sss_palette);
            else if (m_inspector)
                m_waterfall_win->setPalette(m_inspector->currentPaletteIndex());
        }
    };

    m_undo_stack->push(new SetSourceCrsCommand(old_ref, new_ref, apply_crs));

    const std::string display = geo::epsgDisplayName(new_ref);
    const int n = static_cast<int>(currentProject()->layers().size());
    appendJobMessage(tr("Source CRS set to %1 — applied to %2 layer(s)")
        .arg(QString::fromStdString(display)).arg(n));
    recordActivity(ActivityKind::CrsChange,
        tr("Source CRS → %1").arg(QString::fromStdString(display)));
}

void MainWindow::onWaterfallNavProcessLine(NavProcessingParams params)
{
    // Target the actively selected layer — the panel reflects it. The waterfall's
    // current line can be stale (the map/tree selection moved on, or the window is
    // closed), so prefer the active layer and only fall back to the viewer when
    // nothing is selected. Store + apply live (model-owned; persisted).
    std::string lid = activeLayerId();
    if (lid.empty() && m_waterfall_win) lid = m_waterfall_win->currentLayerId();
    if (lid.empty() || !currentProject()) return;
    auto* layer = currentProject()->findLayer(lid);
    if (!layer || layer->modality != app::Modality::Sidescan) return;

    layer->nav_state      = params;
    layer->nav_customized = true;
    markProjectDirty();
    if (m_waterfall_win) m_waterfall_win->applyNavToLine(params);

    // Rebuild the SSS map preview with the new nav — same correction the waterfall
    // uses — but only if this layer is currently on the map (mirrors the SBP path).
    if (m_sss_ctrl && m_map_view && m_map_view->layerData(lid))
        m_sss_ctrl->reloadLayer(lid);

    recordActivity(ActivityKind::NavCorrection,
        tr("Nav corrections applied to %1")
            .arg(QString::fromStdString(layer->label)));
}

void MainWindow::onWaterfallNavProcessAllLines(NavProcessingParams params)
{
    if (!currentProject()) return;

    // Snapshot per-layer nav_state before/after so undo restores layers that were
    // uncustomized. The command's apply writes back into the model (the single
    // source of truth) rather than a MainWindow-side map.
    using ParamMap = std::unordered_map<std::string, NavProcessingParams>;
    ParamMap old_state, new_state;
    int n = 0;
    for (const auto& layer : currentProject()->layers()) {
        if (!layer || layer->modality != app::Modality::Sidescan) continue;
        if (layer->nav_customized) old_state[layer->id] = layer->nav_state;
        new_state[layer->id] = params;
        ++n;
    }
    if (n == 0) return;

    auto apply = [this](const ParamMap& map) {
        if (!currentProject()) return;
        for (const auto& l : currentProject()->layers()) {
            if (!l || l->modality != app::Modality::Sidescan) continue;
            const auto it = map.find(l->id);
            l->nav_state      = (it != map.end()) ? it->second : NavProcessingParams{};
            l->nav_customized = (it != map.end());
        }
        markProjectDirty();
        if (m_waterfall_win)
            applyStoredNavParams(m_waterfall_win->currentLayerId());
        // Rebuild every loaded SSS layer's map preview with the new nav.
        if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
    };
    m_undo_stack->push(new SetNavParamsAllCommand(
        std::move(old_state), std::move(new_state), std::move(apply)));

    appendJobMessage(tr("Nav corrections stored for %1 sidescan line(s) — applied on next open").arg(n));
    recordActivity(ActivityKind::NavCorrection,
        tr("Nav corrections stored for %1 line(s)").arg(n));
}

void MainWindow::applyStoredNavParams(const std::string& layer_id)
{
    if (!m_waterfall_win || layer_id.empty() || !currentProject()) return;
    // Apply the layer's nav state — defaults when uncustomized — so switching to a
    // line without corrections clears any carried over from the previous line.
    const auto* layer = currentProject()->findLayer(layer_id);
    m_waterfall_win->applyNavToLine(layer ? layer->nav_state : NavProcessingParams{});
}

// Explicit "commit corrections to data" (SeaView-style mosaic bake). Ordinary
// gain/imaging Apply is display-state only; this is the deliberate, confirmable
// step that writes the corrected .dlpd sidecars (originals preserved) so the map
// mosaic and exports reflect the full corrections. Covers SSS + SBP layers that
// have applied corrections.
void MainWindow::onBakeCorrections()
{
    if (!currentProject() || !m_corr_op) return;

    int n = 0;
    for (const auto& l : currentProject()->layers()) {
        if (!l) continue;
        if (l->modality == app::Modality::Sidescan && l->sss_display_state.customized)
            ++n;
        else if (l->modality == app::Modality::SubBottom
                 && (l->sbp_display_state.gain_customized
                     || l->sbp_display_state.signal_customized))
            ++n;
    }
    if (n == 0) {
        QMessageBox::information(this, tr("Bake Corrections"),
            tr("No layers have applied gain/imaging corrections to bake.\n\n"
               "Adjust gain/imaging and Apply first, then bake to write the corrected "
               "data into the project's files."));
        return;
    }
    if (QMessageBox::question(this, tr("Bake Corrections"),
            tr("Write the applied gain/imaging corrections into the project's data "
               "files (.dlpd) for %1 layer(s)?\n\n"
               "This creates processed copies — the original parsed data is kept. Use "
               "it to commit corrections for export and the high-quality map mosaic.").arg(n),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    // Bake through the capped batch: one job per customized layer, each baking its
    // own display state, dispatched ≤ kMaxConcurrent at a time so the heavy
    // read+correct+write jobs honour D-14 (the per-layer applySSS/applySBP path
    // fires immediately on the global pool and would flood the cap).
    m_corr_op->bakeCustomized(*currentProject());
    appendJobMessage(
        tr("Baking corrections into %1 layer(s) — watch the bottom panel.").arg(n));
    recordActivity(ActivityKind::NavCorrection,
                   tr("Baked corrections into %1 layer(s)").arg(n));
}

void MainWindow::onChannelChanged(DisplayChannel ch)
{
    // Sync the inspector combo (may already be the source — uses QSignalBlocker).
    if (m_inspector)
        m_inspector->setChannel(ch);

    // Apply to the waterfall.
    if (m_waterfall_win)
        m_waterfall_win->setDisplayChannel(ch);

    // Persist into the active layer's display state so it is restored on layer switch.
    if (currentProject() && !activeLayerId().empty()) {
        if (auto* layer = currentProject()->findLayer(activeLayerId())) {
            layer->sss_display_state.params.display_channel = ch;
            markProjectDirty();
        }
    }
}

void MainWindow::onPaletteChanged(int idx)
{
    // Sync the Properties inspector (may be the source — setPalette uses QSignalBlocker).
    if (m_inspector)
        m_inspector->setPalette(idx);

    // Sync the waterfall (may be the source — setPalette checks current index first).
    if (m_waterfall_win)
        m_waterfall_win->setPalette(idx);

    // Sync the SBP window and keep the right panel in sync.
    if (m_sbp_win) {
        m_sbp_win->setPalette(idx);
        if (m_modal_host)
            m_modal_host->setSbpParams(m_sbp_win->displayParams());
    }

    // Rebuild the map swath preview with the new palette.
    if (m_sss_ctrl)
        m_sss_ctrl->setPaletteIndex(idx);

    static const char* kPaletteNames[] = {
        "Thermal", "Greyscale", "Ocean", "Copper", "Inverted",
        "Viridis", "Plasma", "Midnight", "Sand", "Spectrum"
    };
    const char* name = (idx >= 0 && idx < 10) ? kPaletteNames[idx] : "Unknown";
    recordActivity(ActivityKind::Palette,
        tr("Palette changed to %1").arg(QLatin1String(name)));
}

} // namespace dolphin::ui
