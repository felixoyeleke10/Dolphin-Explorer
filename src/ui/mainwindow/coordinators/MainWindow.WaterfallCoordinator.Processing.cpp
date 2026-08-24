// MainWindow.WaterfallCoordinator.Processing.cpp — layer processing applied from
// the waterfall: source-CRS change, nav corrections (single + all lines), the
// explicit "bake corrections into data" command, and global palette propagation.
// Split out of MainWindow.WaterfallCoordinator.cpp (the window lifecycle).
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
#include "ui/systems/DisplayStateManager.h"
#include <QPushButton>
#include <QStringList>
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"

#include <QMessageBox>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace dolphin::ui {

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
    const std::string source_id = layer->source_id;

    // Source CRS is a source-file property — apply to every layer and source in
    // the project.  The project display CRS (map target) is left unchanged; the
    // normalisation step reprojects source coordinates into it on reload.
    auto apply_crs = [this, ref_id, source_id](const core::SpatialRef& ref) {
        if (!currentProject()) return;
        for (const auto& l : currentProject()->layers())
            if (l && l->source_id == source_id) l->source_spatial_ref = ref;
        if (auto* src = currentProject()->findSource(source_id))
            src->source_spatial_ref = ref;
        markProjectDirty();
        m_session_ctrl->autoSave();
        auto* lyr = currentProject()->findLayer(ref_id);
        if (!ref_id.empty() && ref_id != activeLayerId()) {
            m_layer_ctrl->setActiveLayer(ref_id);
            m_app_state->setSelection({ref_id, lyr ? lyr->modality : app::Modality::Unknown});
            if (m_inspector && lyr) m_inspector->showLayer(lyr);
        }
        if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
        if (m_waterfall_win && lyr) {
            const auto* src = currentProject()->findSource(lyr->source_id);
            m_waterfall_win->setLayer(lyr,
                                      src ? src->path : std::string{},
                                      src ? src->size_bytes : 0);
            applyStoredNavParams(lyr->id);
            // Restore the global SSS palette after CRS-triggered reload.
        }
    };

    m_undo_stack->push(new SetSourceCrsCommand(old_ref, new_ref, apply_crs));

    const std::string display = geo::epsgDisplayName(new_ref);
    const int n = static_cast<int>(std::count_if(
        currentProject()->layers().cbegin(), currentProject()->layers().cend(),
        [&source_id](const auto& l) { return l && l->source_id == source_id; }));
    appendJobMessage(tr("Source CRS set to %1 — applied to %2 layer(s)")
        .arg(QString::fromStdString(display)).arg(n));
    recordActivity(ActivityKind::CrsChange,
        tr("Source CRS → %1").arg(QString::fromStdString(display)));
}


void MainWindow::applyStoredNavParams(const std::string& layer_id)
{
    if (!m_waterfall_win || layer_id.empty() || !currentProject()) return;
    // Apply the layer's nav state — defaults when uncustomized — so switching to a
    // line without corrections clears any carried over from the previous line.
    const auto* layer = currentProject()->findLayer(layer_id);
    m_waterfall_win->applyNavToLine(layer ? layer->nav_state : NavProcessingParams{});
}

// --- Single shared Apply bar (whole tools panel) -----------------------------
// One "Apply to Line" / "Apply to All" at the bottom of the panel replaces the
// per-section buttons. It gathers every visible tool section's settings for the
// active sensor and applies them in a single map rebuild.
void MainWindow::onApplyToolsToLine() { applyActiveTools(/*all_lines*/ false); }
void MainWindow::onApplyToolsToAll()  { applyActiveTools(/*all_lines*/ true); }

void MainWindow::updateToolsApplyBar()
{
    if (!m_tools_apply_bar) return;
    using M = app::Modality;
    bool show = false;
    M mod = M::Unknown;
    if (currentProject() && !activeLayerId().empty())
        if (const auto* l = currentProject()->findLayer(activeLayerId())) {
            mod  = l->modality;
            show = (mod == M::Sidescan || mod == M::SubBottom);
        }
    m_tools_apply_bar->setVisible(show);
    if (!show || !m_tools_apply_line) return;

    // Reflect the multi-selection in the primary button: when more than one line of
    // the active modality is selected it reads "Apply to Selected (N)" — exactly the
    // set applyActiveTools(false) will target.
    int n = 0;
    if (m_line_list && currentProject())
        for (const auto& id : m_line_list->selectedLayerIds())
            if (const auto* sl = currentProject()->findLayer(id);
                sl && sl->modality == mod)
                ++n;
    m_tools_apply_line->setText(n > 1 ? tr("Apply to Selected (%1)").arg(n)
                                      : tr("Apply to Line"));
}

void MainWindow::applyActiveTools(bool all_lines)
{
    auto* proj = currentProject();
    if (!proj || !m_display_state) return;
    if (m_import_overlay && m_viewport_host)
        m_import_overlay->attachTo(m_viewport_host);
    const std::string active = activeLayerId();
    const auto* layer = proj->findLayer(active);
    if (!layer) return;
    using M = app::Modality;
    const M mod = layer->modality;

    // Target lines:
    //   Apply to All     → every line of the active line's modality.
    //   Apply to Line(s) → the tree's selected rows (of that modality); when nothing
    //                      multi-selected, just the active line.
    std::vector<std::string> target_ids;
    if (all_lines) {
        for (const auto& l : proj->layers())
            if (l && l->modality == mod) target_ids.push_back(l->id);
    } else {
        if (m_line_list)
            for (const auto& id : m_line_list->selectedLayerIds())
                if (const auto* sl = proj->findLayer(id); sl && sl->modality == mod)
                    target_ids.push_back(id);
        if (target_ids.empty() && !active.empty())
            target_ids.push_back(active);
    }
    if (target_ids.empty()) return;
    const int sel_n = static_cast<int>(target_ids.size());

    // Open a processing-dialog card per line that will actually rebuild now, tracking
    // each on m_tools_apply_layers so progress/finish handlers update + close them.
    auto openCards = [this, proj](const std::vector<std::string>& rebuild,
                                  const QString& summary, const QString& tag,
                                  bool tools_present) {
        if (!m_import_overlay) return;
        m_tools_apply_layers.clear();
        m_import_overlay->setQueueTotal(static_cast<int>(rebuild.size()));
        for (const auto& id : rebuild) {
            const auto* l = proj->findLayer(id);
            const QString name = l ? QString::fromStdString(l->label)
                                   : QString::fromStdString(id);
            m_import_overlay->addJob(id, tr("%1 — %2").arg(name, summary), tag, 0.f);
            m_import_overlay->updateJob(id, 0, tr("Waiting…"));
            m_tools_apply_layers[id] = tools_present ? summary : QString();
        }
    };

    if (mod == M::Sidescan) {
        // Gather amplitude/display (gain+imaging) and nav (smoothing/layback+attitude)
        // from every SSS section into one combined params set.
        WaterfallParams disp = layer->sss_display_state.params;
        if (m_gain_panel)    m_gain_panel->writeInto(disp);
        if (m_imaging_panel) m_imaging_panel->writeInto(disp);
        NavProcessingParams nav = layer->nav_state;
        if (m_nav_panel)     m_nav_panel->writeInto(nav);
        if (m_heading_panel) m_heading_panel->writeInto(nav);

        // One workflow owns scope filtering and every SSS model mutation.
        SidescanProcessingCoordinator::Result commit_result;
        if (m_sss_processing)
            commit_result = m_sss_processing->commit(proj, target_ids, disp, &nav);

        // Applied model state is authoritative. Discard per-line editor drafts so
        // selecting a target line reloads the checked tools and exact persisted
        // values instead of an older uncommitted UI snapshot.
        for (const auto& id : target_ids) m_sss_control_drafts.erase(id);
        if (m_gain_panel) m_gain_panel->setParams(disp);
        if (m_imaging_panel) m_imaging_panel->setParams(disp);

        // An explicitly empty SSS chain means "use imported data" for every
        // selected target, independent of which workflow produced its sidecar.
        for (const auto& id : commit_result.revert_layer_ids)
            onRevertProcessedLayer(id);
        auto invalidations =
            SidescanProcessingCoordinator::invalidationsFor(commit_result);

        // Keep the live waterfall in sync if it is open.
        if (m_waterfall_win && m_waterfall_win->isVisible()
                && std::find(target_ids.begin(), target_ids.end(),
                             m_waterfall_win->currentLayerId()) != target_ids.end()) {
            m_waterfall_win->applyExternalParams(disp);
            applyStoredNavParams(m_waterfall_win->currentLayerId());
        }

        QStringList tools;
        if (disp.tvg.enabled)            tools << QStringLiteral("TVG");
        if (disp.agc.enabled)            tools << QStringLiteral("AGC");
        if (disp.arc.enabled)            tools << QStringLiteral("ARC");
        if (disp.arn.enabled)            tools << QStringLiteral("ARN");
        if (disp.destripe.enabled)       tools << QStringLiteral("Destripe");
        if (disp.beam_pattern.enabled)   tools << QStringLiteral("Beam Pattern");
        if (disp.ml_enhance.enabled)     tools << QStringLiteral("Adaptive Contrast");
        if (nav.smooth_enabled || nav.layback_enabled ||
            nav.heading_offset_deg != 0.f || nav.pitch_offset_deg != 0.f ||
            nav.roll_offset_deg != 0.f)  tools << tr("Nav");
        const QString summary = tools.isEmpty() ? tr("unprocessed")
                                                : tools.join(QStringLiteral(", "));

        // Only lines currently on the map rebuild now (cards mirror that); others
        // pick up the stored params lazily when first activated.
        std::vector<std::string> rebuild;
        if (m_sss_ctrl) {
            const auto plan = coalesceSidescanInvalidations(invalidations);
            const auto loaded = m_sss_ctrl->loadedLayers();
            for (const auto& id : loaded) {
                const auto it = plan.find(id);
                if (it != plan.end()
                        && it->second != SidescanRefreshAction::Recolour)
                    rebuild.push_back(id);
            }
        }

        // ONE card per rebuilding line; advances through phases + closes when done.
        openCards(rebuild, summary, QStringLiteral("COR"), !tools.isEmpty());

        // Line-by-line rebuild (keeps the old mosaic until ready). Routed through
        // applyLiveCorrections → prebuildTier so prebuildTierProgress/Finished update
        // and close each line's dialog card.
        if (m_sss_ctrl) m_sss_ctrl->applyInvalidations(invalidations);

        recordActivity(ActivityKind::DisplayParams,
            all_lines ? tr("Tools applied to all sidescan lines")
                      : tr("Tools applied to %n sidescan line(s)", nullptr, sel_n));
    }
    else if (mod == M::SubBottom) {
        auto* gm = m_modal_host ? m_modal_host->sbpGainModule()   : nullptr;
        auto* sm = m_modal_host ? m_modal_host->sbpSignalModule() : nullptr;
        const app::SbpGainParams gain = gm ? gm->currentParams()
                                           : layer->sbp_display_state.gain;
        const app::SbpSignalParams signal = sm ? sm->currentParams()
                                                : layer->sbp_display_state.signal;
        NavProcessingParams nav = layer->nav_state;
        if (m_sbp_nav_panel)     m_sbp_nav_panel->writeInto(nav);
        if (m_sbp_heading_panel) m_sbp_heading_panel->writeInto(nav);

        // Store gain/signal/nav on every target line.
        for (const auto& id : target_ids) {
            if (gm) m_display_state->setLayerSbpGain(id, gain);
            if (sm) m_display_state->setLayerSbpSignal(id, signal);
            m_display_state->setLayerNav(id, nav);
        }
        std::unordered_set<std::string> reverted;
        if (!hasSbpProcessing(gain, signal)) {
            for (const auto& id : target_ids) {
                const auto* target = proj->findLayer(id);
                if (target && target->pipeline_applied) {
                    reverted.insert(id);
                    onRevertProcessedLayer(id);
                }
            }
        }
        // Push gain/signal live to the SBP window (display-state only — no .dlpd bake).
        if (m_sbp_win) {
            if (gm) m_sbp_win->applyGainParams(gain);
            if (sm) m_sbp_win->applySignalParams(signal);
            m_sbp_win->applyNavToLine(nav);
        }

        QStringList tools;
        if (gm) { const auto& g = gain;
            if (g.static_gain_en) tools << QStringLiteral("Static Gain");
            if (g.agc_en)         tools << QStringLiteral("AGC");
            if (g.normalize_en)   tools << QStringLiteral("Normalize"); }
        if (sm) { const auto& s = signal;
            if (s.envelope_en)    tools << QStringLiteral("Envelope");
            if (s.dc_removal_en)  tools << QStringLiteral("DC Removal");
            if (s.bandpass_en)    tools << QStringLiteral("Bandpass"); }
        if (nav.smooth_enabled || nav.layback_enabled ||
            nav.heading_offset_deg != 0.f || nav.pitch_offset_deg != 0.f ||
            nav.roll_offset_deg != 0.f)  tools << tr("Nav");
        const QString summary = tools.isEmpty() ? tr("display only")
                                                : tools.join(QStringLiteral(", "));

        // Only lines already on the map rebuild now; others apply lazily on select.
        std::vector<std::string> rebuild;
        for (const auto& id : target_ids)
            if (!reverted.count(id) && m_map_view && m_map_view->layerData(id))
                rebuild.push_back(id);

        openCards(rebuild, summary, QStringLiteral("SBP"), !tools.isEmpty());

        // Line-by-line profile rebuild (symmetric with the SSS branch above).
        applySbpLiveCorrections(rebuild);

        recordActivity(ActivityKind::NavCorrection,
            all_lines ? tr("Tools applied to all sub-bottom lines")
                      : tr("Tools applied to %n sub-bottom line(s)", nullptr, sel_n));
    }

    // Confirm in the status bar — important when the targeted line(s) aren't on the
    // map (nothing rebuilds visibly), so the user still sees the apply registered.
    if (m_status_bar)
        m_status_bar->showJobMessage(
            all_lines ? tr("Tools applied to all lines")
                      : tr("Tools applied to %n line(s)", nullptr, sel_n));
}

// Explicit "commit corrections to data" (SeaView-style mosaic bake). Ordinary
// gain/imaging Apply is display-state only; this is the deliberate, confirmable
// step that writes the corrected .dlpd sidecars (originals preserved) so the map
// mosaic and exports reflect the full corrections. Covers SSS + SBP layers that
// have applied corrections.
void MainWindow::onBakeCorrections()
{
    if (m_import_overlay && m_viewport_host)
        m_import_overlay->attachTo(m_viewport_host);
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

void MainWindow::onPaletteChanged(int idx)
{
    // Sync the Properties inspector (may be the source — setPalette uses QSignalBlocker).
    if (m_inspector)
        m_inspector->setPalette(idx);

    // Sync the waterfall (may be the source — setPalette checks current index first).
    // Sync the SBP window (the Views panel mirrors via refreshViewsPanel).
    // Global map palette → through the display-state manager: it persists the choice
    // and emits displayStateChanged({}, Palette); our handler applies it to the map.
    if (m_display_state)
        m_display_state->setMapPalette(idx);

    static const char* kPaletteNames[] = {
        "Thermal", "Greyscale", "Ocean", "Copper", "Inverted",
        "Viridis", "Plasma", "Midnight", "Sand", "Spectrum"
    };
    const char* name = (idx >= 0 && idx < 10) ? kPaletteNames[idx] : "Unknown";
    recordActivity(ActivityKind::Palette,
        tr("Palette changed to %1").arg(QLatin1String(name)));
}

} // namespace dolphin::ui
