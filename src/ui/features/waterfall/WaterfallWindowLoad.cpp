// WaterfallWindowLoad.cpp — async window load from artifact store.

#include <QDebug>
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "ui/features/waterfall/processing/WaterfallPingAssembler.h"
#include "app/layers/DataLayer.h"
#include "app/services/ImportService.h"
#include "app/tasks/OperationManager.h"
#include "core/Artifact.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QScrollBar>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Async data loading
// -----------------------------------------------------------------------------

void WaterfallWindow::loadWindow(int abs_row)
{
    if (!m_layer || !m_import_service) {
        m_view->clear();
        m_vscroll->setRange(0, 0);
        return;
    }

    if (m_layer->modality != app::Modality::Sidescan) {
        m_view->clear();
        m_vscroll->setRange(0, 0);
        setDataState(ViewerDataState::Failed);
        return;
    }

    if (!m_op_mgr) return;
    setDataState(ViewerDataState::Loading);
    m_status_left->setText("⋯");   // ⋯ — minimal "loading" indicator
    startProgress();

    // Snapshot plain-data fields — DataLayer is a QObject and must not be
    // accessed from a background thread.
    const std::string store_path   = m_layer->artifact_store_path;
    const std::string store_format = m_layer->artifact_store_format;
    const core::ArtifactIndex idx  = m_layer->artifact_index;
    const std::string source_path  = m_source_path;
    auto* svc                      = m_import_service;
    const int total_entries        = m_total_ssc_entries;

    const float epr         = m_entries_per_row;
    const int center_entry  = std::clamp(
        static_cast<int>(abs_row * epr), 0, std::max(0, total_entries - 1));
    const int first_entry   = std::max(0,
        std::min(center_entry - m_window_size / 2, total_entries - m_window_size));
    const int entries_in_window = std::min(m_window_size, total_entries - first_entry);
    const int target_local  = std::max(0,
        static_cast<int>((center_entry - first_entry) / epr));

    const bool reset_view   = m_reset_view_next;
    m_reset_view_next       = false;

    // Capture the layer's source CRS so the background thread can apply it to
    // pings that don't carry an exact spatial_ref of their own.
    core::SpatialRef layer_src_ref = m_layer->source_spatial_ref;
    const bool apply_layer_crs =
        layer_src_ref.exact && core::spatialRefIsProjected(layer_src_ref);

    // Snapshot display params and seabed config so the background pipeline
    // can assemble and detect seabed without touching UI-thread state.
    const WaterfallParams  snap_params         = m_view->params();
    const SeabedAutoParams snap_seabed_params  = m_analysis ? m_analysis->currentSeabedAutoParams()
                                                             : m_view->seabedAutoParams();
    const bool             snap_seabed_enabled = m_view->seabedEnabled();

    struct LoadResult {
        std::vector<core::SidescanPing>  raw_pings;
        WaterfallView::WfPipelineResult  pipeline;
        bool ok          = false;  // full success
        bool load_failed = false;  // disk/parse exception (vs. simply no valid pings)
    };

    const float selected_hz  = m_selected_frequency_hz;
    const int   window_size  = m_window_size;

    // Keyed "wf:pipeline" so load supersedes (and is superseded by) repipe/nav
    // runs; heavy so the disk load respects the D-14 cap; auto-tracked in
    // DiagnosticsHub. The bg fn reports via flags so on_done always runs and can
    // set Failed — a skipped on_done would leave the busy state stuck.
    m_op_mgr->run<LoadResult>(
        tr("Loading waterfall window"),
        [svc, store_path, store_format, idx, source_path, center_entry,
         layer_src_ref, apply_layer_crs, selected_hz, window_size,
         snap_params, snap_seabed_params, snap_seabed_enabled]
        (app::CancellationToken cancel) -> LoadResult {
            LoadResult result;
            try {
                auto raw = svc->loadSidescanWindowFromStore(
                    store_path, store_format, idx, source_path,
                    static_cast<int64_t>(center_entry), window_size, selected_hz);

                if (cancel.isCancelled()) return result;

                // Ping-level frequency guard: drop any non-target band before the
                // assembler sees it (a cross-band port+stbd pair corrupts seabed).
                if (selected_hz > 0.f) {
                    std::vector<float> ping_bands;
                    for (const auto& p : raw) {
                        if (p.frequency_hz <= 0.f) continue;
                        bool found = false;
                        for (float b : ping_bands)
                            if (std::fabs(b - p.frequency_hz) < 1.f) { found = true; break; }
                        if (!found) ping_bands.push_back(p.frequency_hz);
                    }
                    if (ping_bands.size() >= 2) {
                        float target = ping_bands.front();
                        for (float b : ping_bands)
                            if (std::fabs(b - selected_hz) < std::fabs(target - selected_hz))
                                target = b;
                        raw.erase(
                            std::remove_if(raw.begin(), raw.end(),
                                [target](const core::SidescanPing& p) {
                                    return p.frequency_hz > 0.f
                                        && std::fabs(p.frequency_hz - target) >= 1.f;
                                }),
                            raw.end());
                    }
                }

                // Mirror the coordinate normalisation SidescanViewController applies.
                if (apply_layer_crs) {
                    for (auto& ping : raw) {
                        if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                            ping.nav.spatial_ref = layer_src_ref;
                    }
                }
                auto normalised = geo::normalizeSidescanPingsForMap(
                    std::move(raw), core::makeWgs84SpatialRef());

                WaterfallPingAssembler::sanitize(normalised);
                if (normalised.empty()) {
                    qWarning("[WF-LOAD] all pings rejected by sanitize — aborting load");
                    return result;   // ok=false, load_failed=false → "no valid pings"
                }
                if (cancel.isCancelled()) return result;

                // TVG/ARC/AGC → assemble → beam/ARN/destripe/ML → seabed → stretch,
                // entirely off the UI thread.
                result.raw_pings = std::move(normalised);
                result.pipeline  = WaterfallView::runPipeline(result.raw_pings,
                                                              snap_params,
                                                              snap_seabed_params,
                                                              snap_seabed_enabled);
                result.ok = true;
            } catch (...) {
                result.load_failed = true;
            }
            return result;
        },
        [this, first_entry, entries_in_window, target_local, reset_view,
         snap_params, snap_seabed_params, snap_seabed_enabled](LoadResult res) {
            m_status_left->clear();
            finishProgress();

            if (!res.ok) {
                m_view->clear();
                setDataState(ViewerDataState::Failed);
                m_status_left->setText(res.load_failed
                    ? tr("Failed to load data")
                    : tr("No valid pings — check slant range or source data"));
                return;
            }

            // Detect pipeline params that changed while the window was loading.
            bool pipeline_stale = false;
            WaterfallParams current_params;
            if (m_analysis && m_inspector) {
                current_params = m_analysis->currentParams(m_inspector->currentPaletteIndex());
                current_params.display_channel = m_display_channel;
                pipeline_stale =
                    current_params.tvg                    != snap_params.tvg
                    || current_params.agc                 != snap_params.agc
                    || current_params.arc                 != snap_params.arc
                    || current_params.arn                 != snap_params.arn
                    || current_params.destripe            != snap_params.destripe
                    || current_params.beam_pattern        != snap_params.beam_pattern
                    || current_params.ml_enhance          != snap_params.ml_enhance
                    || current_params.slant_range_correction != snap_params.slant_range_correction
                    || current_params.display_channel        != snap_params.display_channel;
            }

            // Install pre-built rows. Stale case keeps raw_pings for the re-run.
            auto raw_pings = std::move(res.raw_pings);
            if (pipeline_stale)
                m_view->setPreassembledRows(raw_pings,
                                            std::move(res.pipeline), !reset_view);
            else
                m_view->setPreassembledRows(std::move(raw_pings),
                                            std::move(res.pipeline), !reset_view);

            m_view->setSeabedAutoParamsOnly(snap_seabed_params, snap_seabed_enabled);
            pushParams();

            const int actual_rows = m_view->rowCount();
            if (actual_rows >= 20 && entries_in_window > 0) {
                const float r = static_cast<float>(entries_in_window) / actual_rows;
                m_entries_per_row = std::clamp(r, 1.0f, 2.1f);
            }
            m_window_first_row = static_cast<int>(
                first_entry / std::max(m_entries_per_row, 1.0f));
            if (actual_rows > 0)
                m_view->scrollToRow(std::clamp(target_local, 0, actual_rows - 1));

            refreshInspector();
            refreshContactOverlay();

            // Re-run the pipeline with corrected params so stale rows are replaced.
            // Same "wf:pipeline" key so a newer load/repipe still supersedes it.
            if (pipeline_stale) {
                setDataState(ViewerDataState::Processing);
                m_op_mgr->run<LoadResult>(
                    tr("Reprocessing waterfall window"),
                    [raw = std::move(raw_pings), p = current_params,
                     sp = snap_seabed_params, se = snap_seabed_enabled]
                    (app::CancellationToken) mutable -> LoadResult {
                        LoadResult r;
                        try {
                            r.pipeline  = WaterfallView::runPipeline(raw, p, sp, se);
                            r.raw_pings = std::move(raw);
                            r.ok = true;
                        } catch (...) { r.load_failed = true; }
                        return r;
                    },
                    [this](LoadResult r) {
                        if (!r.ok) { setDataState(ViewerDataState::Failed); return; }
                        m_view->setPreassembledRows(std::move(r.raw_pings),
                                                    std::move(r.pipeline),
                                                    /*preserve_view=*/true);
                        pushParams();
                        setDataState(ViewerDataState::Ready);
                    },
                    "wf:pipeline",
                    /*heavy=*/false);
            } else {
                setDataState(ViewerDataState::Ready);
            }
        },
        "wf:pipeline",
        /*heavy=*/true);
}

} // namespace dolphin::ui
