// MainWindow.WaterfallCoordinator.Processing.cpp — layer processing applied from
// the waterfall: source-CRS change, nav corrections (single + all lines), the
// explicit "bake corrections into data" command, and global palette propagation.
// Split out of MainWindow.WaterfallCoordinator.cpp (the window lifecycle).
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/project/Project.h"
#include "app/project/ProjectTransaction.h"
#include "app/layers/DataLayer.h"
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"

#include <QMessageBox>

#include <algorithm>
#include <unordered_map>

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
            // Restore the global SSS palette after CRS-triggered reload.
            if (m_display_state)
                m_waterfall_win->setPalette(m_display_state->mapPalette());
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

    if (m_display_state) m_display_state->setLayerNav(lid, params);  // mutate + notify (marks dirty)
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
            // Through the display-state authority (mutate + notify; marks dirty per layer).
            if (m_display_state) {
                if (it != map.end()) m_display_state->setLayerNav(l->id, it->second);
                else                 m_display_state->clearLayerNav(l->id);
            }
        }
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
