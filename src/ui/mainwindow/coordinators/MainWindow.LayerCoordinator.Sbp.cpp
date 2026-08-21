// MainWindow.LayerCoordinator.Sbp.cpp — sub-bottom map construction helpers.
#include "ui/mainwindow/MainWindow.h"

#include "app/display/NavCorrection.h"
#include "app/corrections/SubBottomCorrectionAlgorithms.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "app/services/ImportService.h"
#include "ui/bottom/DiagnosticsHub.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/subbottom/SbpProfileBuild.h"
#include "ui/features/map/subbottom/SubBottomMapDiagnostics.h"
#include "ui/features/subbottom/SubBottomWindow.h"

#include <QPointer>

namespace dolphin::ui {

// Build (or rebuild) the SBP profile map ribbon for one layer, applying any
// nav corrections stored for it. Shared by layer selection and the SBP
// Navigation / Geometry "Apply" actions. Always rebuilds; the caller decides
// whether a rebuild is warranted.
void MainWindow::buildSbpProfileMap(app::DataLayer* layer, const std::string& lane)
{
    if (!layer || !m_map_view || !currentProject()) return;
    if (!layer->index_built || layer->artifact_index.empty())           return;

    core::SpatialRef source_crs = layer->source_spatial_ref;
    if (source_crs.empty()) {
        if (const auto* src = currentProject()->findSource(layer->source_id))
            source_crs = src->source_spatial_ref;
    }
    const core::SpatialRef display_crs = currentProject()->displaySpatialRef();

    // Capture by value — DataLayer must not be accessed on the bg thread.
    const std::string store_path   = layer->artifact_store_path;
    const std::string store_format = layer->artifact_store_format;
    const core::ArtifactIndex index_copy = layer->artifact_index;
    std::string source_path;
    if (const auto* src = currentProject()->findSource(layer->source_id))
        source_path = src->path;
    const std::string lid = layer->id;

    // Display-time nav corrections — single source of truth on the layer.
    const NavProcessingParams nav = layer->nav_state;
    // The 2D profile scalar and 3D curtain are derived from trace amplitudes,
    // so they must consume the same persisted corrections as the SBP waterfall.
    // Per-trace correction flags make this safe for already-baked artifacts.
    const auto gain = layer->sbp_display_state.gain;
    const auto signal = layer->sbp_display_state.signal;

    // Run through OperationManager: keyed so a newer build for this layer
    // supersedes any in-flight one (replacing the old m_pending_sbp_builds guard),
    // and tracked in DiagnosticsHub automatically via the operation→job bridge.
    // NOT heavy: this is display rasterization of an already-imported layer, not
    // an import/decode job, so it must not sit under the D-14 cap (2) — capping it
    // throttled multi-line projects (parse→display felt slow). The global
    // QThreadPool still bounds total concurrency.
    // Per-line progress for a bottom-bar SBP Apply batch: report coarse phases to that
    // line's dialog card (marshalled to the main thread).
    const QPointer<MainWindow> report_owner(this);
    auto report = [report_owner, lid](int pct, const QString& phase) {
        if (!report_owner) return;
        QMetaObject::invokeMethod(report_owner.data(),
            [report_owner, lid, pct, phase]() {
            if (!report_owner || !report_owner->m_import_overlay) return;
            const auto it = report_owner->m_tools_apply_layers.find(lid);
            if (it == report_owner->m_tools_apply_layers.end()) return;
            // Name the actual tools while in the corrections band ("Applying Static
            // Gain, AGC…"); keep the worker's phase text for read/build bands.
            const QString status = (pct >= 60 && pct < 90 && !it->second.isEmpty())
                ? tr("Applying %1…").arg(it->second)
                : phase;
            report_owner->m_import_overlay->updateJob(lid, pct,
                QStringLiteral("%1  %2%").arg(status).arg(pct));
        }, Qt::QueuedConnection);
    };
    m_op_mgr->run<LayerMapData>(
        tr("Building sub-bottom profile map…"),
        [store_path, store_format, index_copy,
         source_path, source_crs, display_crs, nav, gain, signal,
         report](app::CancellationToken cancel) {
            report(15, tr("Reading traces…"));
            auto traces = app::ImportService::loadAllSubBottomTraces(
                store_path, store_format, index_copy, source_path);
            report(70, tr("Applying corrections…"));
            applySbpNavCorrections(traces, nav);   // display-time nav corrections
            if (!app::corrections::applySubBottomCorrections(
                    traces, gain, signal,
                    [&cancel] { return cancel.isCancelled(); }))
                return LayerMapData{};
            report(90, tr("Building profile…"));
            return buildSbpProfileMapData(traces, source_crs, display_crs);
        },
        [this, lid](LayerMapData result) {
            if (!currentProject() || !currentProject()->findLayer(lid)) return;
            if (m_map_view) {
                result.track_stats.layer_visible = m_map_view->isLayerVisible(lid);
                m_map_view->setLayerMapData(lid, result);
            }
            // Seed the 3D curtain palette from the layer's SBP palette (the
            // curtain itself is forwarded via layerDataUpdated → viewport host).
            if (m_viewport_host) {
                if (const auto* l = currentProject()->findLayer(lid))
                    m_viewport_host->setSbpCurtainPalette(
                        l->sbp_palette >= 0 ? l->sbp_palette : 0);
            }
            if (m_diag_hub)
                postSubBottomMapDiagnostics(
                    m_diag_hub, QString::fromStdString(lid), result.track_stats);
        },
        "sbp_profile:" + lid,
        /*heavy=*/false,   // display build, not import/decode — see note above
        // on_finally: mark this line's dialog card done (any outcome) when it is part
        // of a bottom-bar SBP Apply batch.
        [this, lid]() {
            if (m_import_overlay && m_tools_apply_layers.erase(lid) > 0) {
                m_import_overlay->finishJob(lid, tr("Tools applied"));
            }
        },
        lane);
}

void MainWindow::applySbpLiveCorrections(const std::vector<std::string>& layer_ids)
{
    // Line-by-line rebuild (mirrors SidescanViewController::applyLiveCorrections):
    // a cap-1 "sbp:apply" lane processes one profile at a time. Callers pass only the
    // layers that should rebuild now (already on the map); others apply lazily.
    if (!currentProject() || layer_ids.empty()) return;
    if (m_op_mgr) m_op_mgr->setLaneCap("sbp:apply", 1);
    for (const auto& id : layer_ids)
        if (auto* l = currentProject()->findLayer(id))
            buildSbpProfileMap(l, "sbp:apply");
}

void MainWindow::applyStoredSbpNavParams(const std::string& layer_id)
{
    if (!m_sbp_win || layer_id.empty() || !currentProject()) return;
    // Apply the layer's nav state (default when uncustomized) so switching to a
    // line without corrections clears any carried over from the previous line.
    const auto* layer = currentProject()->findLayer(layer_id);
    m_sbp_win->applyNavToLine(layer ? layer->nav_state : NavProcessingParams{});
}

} // namespace dolphin::ui
