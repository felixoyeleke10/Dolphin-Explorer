// SidescanMapLoadTask.cpp — activateLayer: background ping load, coverage build,
//                           preview raster, and finished-handler wiring.
//   Types: QualityParams, SidescanLoadResult → SidescanMapLoadParams.h
#include <QDebug>
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanMapLoadParams.h"
#include "ui/shared/CoordFormat.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "app/services/ImportService.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "geo/GeoUtils.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

using detail::QualityParams;
using detail::SidescanLoadResult;
using detail::paramsForQuality;
using detail::kFullSafeLimit;

// -- activateLayer -------------------------------------------------------------

void SidescanViewController::activateLayer(const std::string& layer_id,
                                           app::Project*      project)
{
    m_active_layer_id = layer_id;
    m_project         = project;

    // Off quality: show nav track only, no ping I/O.
    if (m_quality == MapSonarQuality::Off) {
        if (m_map_view) m_map_view->setActiveLayer(layer_id);
        if (m_status_ping) m_status_ping->setText(tr("Map sonar off"));
        emit loadingFinished();
        return;
    }

    if (m_loaded_layers.count(layer_id)) {
        emit loadingFinished();
        return;
    }

    auto* layer = project ? project->findLayer(layer_id) : nullptr;
    if (!layer) {
        deactivate(false);
        emit loadingFinished();
        return;
    }

    // Defensive guard: this controller only processes Sidescan layers.
    // Non-sidescan modalities (SBP, MAG, MBES) are routed through
    // MainWindow.LayerCoordinator which extracts their nav track directly.
    if (layer->modality != app::Modality::Sidescan &&
        layer->modality != app::Modality::Unknown) {
        emit loadingFinished();
        return;
    }

    if (!layer->index_built) {
        if (m_status_ping) m_status_ping->setText(tr("Layer not yet indexed"));
        emit loadingFinished();
        return;
    }
    if (layer->sidescanCount() == 0) {
        if (m_status_ping)
            m_status_ping->setText(
                tr("No sidescan data  (%1 artifacts total)").arg(layer->artifactCount()));
        emit loadingFinished();
        return;
    }

    auto* src = project->findSource(layer->source_id);

    // -- Snapshot all fields needed by the background task ---------------------
    const std::string store_path     = layer->artifact_store_path;
    const std::string store_format   = layer->artifact_store_format;
    const core::ArtifactIndex idx    = layer->artifact_index;
    const std::string source_path    = src ? src->path : std::string{};
    const float layer_freq_hz        = layer->frequency_hz;
    const float layer_low_freq_hz    = layer->low_frequency_hz;

    core::SpatialRef layer_src_ref = layer->source_spatial_ref;
    if (layer_src_ref.empty() && src)
        layer_src_ref = src->source_spatial_ref;
    const bool apply_layer_crs =
        layer_src_ref.exact && core::spatialRefIsProjected(layer_src_ref);

    const core::SpatialRef display_ref =
        project ? project->displaySpatialRef() : core::SpatialRef{};

    const QualityParams qp = paramsForQuality(m_quality);

    const int palette_idx = m_palette_idx;

    if (m_status_ping) m_status_ping->setText(tr("Loading…"));

    // Show nav track immediately from the already-loaded index (zero I/O).
    if (m_map_view) {
        m_map_view->setActiveLayer(layer_id);

        // Pre-fit the viewport from the index nav extent so the map is centred
        // on this layer before the background ping load completes.
        const auto ext = idx.navExtent();
        if (ext.valid && !m_map_view->userInteracted()) {
            const bool proj = apply_layer_crs
                              || (!layer_src_ref.empty()
                                  && core::spatialRefIsProjected(layer_src_ref));
            m_map_view->fitToExtent(ext.lon_min, ext.lon_max,
                                    ext.lat_min, ext.lat_max, proj);
        }
    }

    // -- Set up per-layer cancellation and generation guard --------------------
    // Only cancel a previous load of the SAME layer. Other layers' background
    // tasks are unaffected, allowing multiple layers to load concurrently.
    auto& layer_cancel = m_layer_cancel_flags[layer_id];
    if (layer_cancel)
        layer_cancel->store(true, std::memory_order_relaxed);
    layer_cancel = std::make_shared<std::atomic_bool>(false);

    const uint64_t gen = ++m_layer_generations[layer_id];
    auto           cancel = layer_cancel;   // shared ownership into lambda

    // -- Kick off the background load ------------------------------------------
    auto* watcher = new QFutureWatcher<SidescanLoadResult>(this);

    connect(watcher, &QFutureWatcher<SidescanLoadResult>::finished, this,
        [this, watcher, layer_id]() {
            watcher->deleteLater();

            SidescanLoadResult res;
            try {
                res = watcher->result();
            } catch (...) {
                if (m_status_ping)
                    m_status_ping->setText(tr("Failed to load sidescan data"));
                emit loadingFinished();
                return;
            }

            // Discard stale results: only reject if a newer load for this specific
            // layer has been started since this task was launched. Results for
            // layers other than the currently active one are still applied — all
            // layers accumulate on the map independently.
            {
                const auto it = m_layer_generations.find(res.layer_id);
                if (it == m_layer_generations.end() || res.generation != it->second) {
                    emit loadingFinished();
                    return;
                }
            }

            if (res.load_failed || res.raw_count == 0) {
                if (m_status_ping)
                    m_status_ping->setText(
                        tr("Sidescan index OK but no pings loaded — cache may be unreadable"));
                emit loadingFinished();
                return;
            }

            emit loadingFinished();

            if (m_map_view) {
                // Copy diagnostics before the move so we can enrich with
                // view state (visibility, fit, paint rect) after placement.
                NavStats stats = res.layer_data.nav_stats;

                // Extract intensity cache before moving res.layer_data.
                {
                    IntensityCache ic;
                    ic.pixels    = std::move(res.layer_data.intensity_cache);
                    ic.w         = res.layer_data.intensity_w;
                    ic.h         = res.layer_data.intensity_h;
                    ic.disp_low  = res.layer_data.intensity_disp_low;
                    ic.disp_high = res.layer_data.intensity_disp_high;

                    if (ic.valid()) {
                        m_layer_intensity_cache[res.layer_id] = ic;

                        // Also populate the quality-tier cache so this quality
                        // level is immediately available for instant switching.
                        PrebuiltTier& tier =
                            m_quality_tier_cache[res.layer_id]
                                               [static_cast<int>(m_quality)];
                        tier.coverage        = res.layer_data.coverage;
                        tier.nav_track       = res.layer_data.nav_track;
                        tier.lon_min         = res.layer_data.lon_min;
                        tier.lon_max         = res.layer_data.lon_max;
                        tier.lat_min         = res.layer_data.lat_min;
                        tier.lat_max         = res.layer_data.lat_max;
                        tier.is_projected    = res.layer_data.is_projected;
                        tier.preview_reduced = res.layer_data.preview_reduced;
                        tier.nav_stats       = res.layer_data.nav_stats;
                        tier.intensity       = std::move(ic);
                    }
                }

                m_map_view->setLayerMapData(layer_id, std::move(res.layer_data));
                if (layer_id == m_active_layer_id)
                    m_map_view->setActiveLayer(layer_id);
                m_loaded_layers.insert(layer_id);
                m_layer_pings_cache[res.layer_id] = std::move(res.map_pings_cache);

                // Populate stage-2/3 diagnostics from the now-live map view.
                stats.layer_visible  = m_map_view->isLayerVisible(layer_id);
                stats.layer_active   = (layer_id == m_active_layer_id);
                stats.fit_applied    = !m_map_view->userInteracted();

                const QRectF pr      = m_map_view->layerPaintRect(layer_id);
                stats.paint_rect     = pr.toRect();
                stats.paint_onscreen = !pr.isEmpty()
                    && m_map_view->rect().intersects(pr.toRect());

                emit mapDiagnosticsReady(QString::fromStdString(layer_id), stats);

                if (!res.has_sample_nav && m_status_ping) {
                    const qulonglong total_pings = res.total_ssc_entries / 2;
                    m_status_ping->setText(
                        tr("%1 pings — no valid GPS for map display")
                            .arg(total_pings > 0 ? total_pings
                                                 : static_cast<qulonglong>(res.raw_count)));
                    return;
                }
            }

            // -- Status bar ----------------------------------------------------
            if (res.has_sample_nav) {
                if (m_status_pos)
                    m_status_pos->setText(
                        formatPosition(res.sample_lat, res.sample_lon, res.sample_is_proj));
                if (m_status_depth)
                    m_status_depth->setText(
                        QString("Depth  %1 m").arg(res.sample_alt_m, 0, 'f', 1));
            } else {
                if (m_status_pos)   m_status_pos->clear();
                if (m_status_depth) m_status_depth->clear();
            }

            if (m_status_ping) {
                QString ping_text;
                if (res.track_m > 0.0) {
                    const size_t total_port_est = res.total_ssc_entries / 2;
                    const double scale =
                        (res.preview_port_count > 0 && total_port_est > res.preview_port_count)
                        ? static_cast<double>(total_port_est) /
                          static_cast<double>(res.preview_port_count)
                        : 1.0;
                    ping_text = QString("~%1 km").arg(res.track_m * scale / 1000.0, 0, 'f', 2);
                } else {
                    const qulonglong total_pings = res.total_ssc_entries / 2;
                    ping_text = QString("%1 pings").arg(
                        total_pings > 0 ? total_pings
                                       : static_cast<qulonglong>(res.raw_count));
                }
                if (res.quality_reduced)
                    ping_text += tr(" · preview reduced");
                m_status_ping->setText(ping_text);
            }
        });

    if (store_path.empty()) {
        if (m_status_ping)
            m_status_ping->setText(tr("Layer has no artifact store — reimport required"));
        watcher->deleteLater();
        emit loadingFinished();
        return;
    }

    app::ImportService* svc = m_import_service;
    if (!svc) {
        if (m_status_ping) m_status_ping->setText(tr("Import service unavailable"));
        watcher->deleteLater();
        emit loadingFinished();
        return;
    }


    SssGeorefParams         georef_params    = m_georef_params;
    georef_params.slant_range_corrected      = layer->slant_range_corrected;
    const MapSonarQuality   current_quality  = m_quality;

    QFuture<SidescanLoadResult> future = QtConcurrent::run(
        [svc, store_path, store_format, idx, source_path,
         layer_src_ref, apply_layer_crs, display_ref, layer_id,
         layer_freq_hz, layer_low_freq_hz, qp, palette_idx, gen, cancel,
         georef_params, current_quality]()
        -> SidescanLoadResult
        {
            try {
            SidescanLoadResult result;
            result.layer_id   = layer_id;
            result.generation = gen;

            // -- Build bounded preview index -----------------------------------
            core::ArtifactIndex map_idx = idx;

            // For pinned single-band layers, remove the other frequency band.
            if (layer_low_freq_hz == 0.f)
                filterSidescanEntriesByBand(map_idx, layer_freq_hz);

            result.total_ssc_entries =
                map_idx.byType(core::ArtifactType::Sidescan).size();
            const size_t total_groups = result.total_ssc_entries / 2;

            // Thin to the quality-determined ping group cap.
            // max_ping_groups == 0 (Full quality) means "use all pings", but
            // if the file exceeds kFullSafeLimit we fall back to High params.
            size_t effective_cap = qp.max_ping_groups;
            if (effective_cap == 0) {
                if (total_groups > kFullSafeLimit) {
                    effective_cap = paramsForQuality(MapSonarQuality::High).max_ping_groups;
                    result.quality_reduced = true;
                }
                // else: effective_cap stays 0 → no thinning below
            }

            if (effective_cap > 0) {
                auto thin = thinSidescanEntriesForMap(map_idx, effective_cap);
                map_idx.entries.erase(
                    std::remove_if(map_idx.entries.begin(), map_idx.entries.end(),
                        [](const core::ArtifactIndexEntry& e) {
                            return e.type == core::ArtifactType::Sidescan;
                        }),
                    map_idx.entries.end());
                map_idx.entries.insert(map_idx.entries.end(), thin.begin(), thin.end());
            }
            // else: Full quality within safe limit — load all ping groups as-is.

            if (cancel->load(std::memory_order_relaxed)) {
                result.load_failed = true; return result;
            }

            auto raw = svc->loadAllSidescanPingsFromStore(
                store_path, store_format, map_idx, source_path,
                qp.max_samples_per_ping);

            result.raw_count = raw.size();
            if (raw.empty()) {
                result.load_failed = true;
                return result;
            }

            if (apply_layer_crs) {
                for (auto& ping : raw) {
                    if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                        ping.nav.spatial_ref = layer_src_ref;
                }
            }

            if (cancel->load(std::memory_order_relaxed)) {
                result.load_failed = true; return result;
            }

            auto map_pings = geo::normalizeSidescanPingsForMap(
                std::move(raw), display_ref, &result.unresolved_crs);

            // -- Coverage + nav track (always built for CoverageOnly+) ---------
            // is_projected must be set before the build calls: both functions use
            // it to pick degree vs. metre gap thresholds and bbox padding units.
            for (const auto& ping : map_pings)
                if (ping.nav.valid) {
                    result.layer_data.is_projected = ping.nav.is_projected;
                    break;
                }

            buildSwathNavTrack(map_pings, result.layer_data);
            buildSwathCoverage(map_pings, result.layer_data, georef_params);

            // -- Sonar preview image (quality >= Low) --------------------------
            if (qp.max_image_dim > 0 && !cancel->load(std::memory_order_relaxed)) {
                const bool built = buildSwathPreviewImage(
                    map_pings, result.layer_data,
                    qp.max_image_dim, palette_idx, *cancel,
                    georef_params, qp.min_strip_cos, qp.cell_budget_div);
                result.quality_reduced = built && result.layer_data.preview_reduced;
            }
            // -- Build / CRS fields for diagnostics ---------------------------
            result.layer_data.nav_stats.quality_used    =
                result.quality_reduced ? MapSonarQuality::High : current_quality;
            result.layer_data.nav_stats.pings_available = total_groups;
            result.layer_data.nav_stats.memory_reduced  = result.quality_reduced;

            {
                std::string lbl;
                if (layer_src_ref.empty()) {
                    lbl = "unknown";
                } else {
                    const bool is_proj = core::spatialRefIsProjected(layer_src_ref);
                    if (layer_src_ref.exact) {
                        // Confirmed CRS — ID is reliable.
                        if (!layer_src_ref.id.empty()) lbl = layer_src_ref.id + " ";
                        lbl += is_proj ? "projected exact" : "geographic exact";
                    } else {
                        // Auto-detected — softer wording to avoid false confidence.
                        lbl = is_proj ? "inferred projected" : "inferred geographic";
                        if (!layer_src_ref.id.empty())
                            lbl += " (from " + layer_src_ref.id + ")";
                    }
                    if (apply_layer_crs) lbl += " [user override]";
                }
                result.layer_data.nav_stats.crs_label = std::move(lbl);
            }
            if (!result.unresolved_crs.empty())
                result.layer_data.nav_stats.unsupported_crs_id =
                    result.unresolved_crs.front().id;

            // -- Pre-compute status bar values ---------------------------------
            for (const auto& ping : map_pings) {
                if (ping.nav.valid) {
                    result.has_sample_nav = true;
                    result.sample_lat     = ping.nav.lat;
                    result.sample_lon     = ping.nav.lon;
                    result.sample_alt_m   = ping.nav.altitude_m;
                    result.sample_is_proj = ping.nav.is_projected;
                    break;
                }
            }
            {
                bool           have_prev = false;
                core::NavPoint prev_nav;
                for (const auto& ping : map_pings) {
                    if (ping.channel != core::SidescanChannel::Port || !ping.nav.valid)
                        continue;
                    ++result.preview_port_count;
                    if (have_prev)
                        result.track_m += geo::navDistanceMetres(prev_nav, ping.nav);
                    prev_nav  = ping.nav;
                    have_prev = true;
                }
            }

            // Keep normalized pings so a palette change can re-rasterize without
            // re-reading from disk.  Moved here (after all stats loops) so the
            // vector is not consumed before we need it above.
            result.map_pings_cache = std::move(map_pings);

            return result;

            } catch (const std::exception& e) {
                qWarning() << "SidescanViewController BG: exception:" << e.what()
                           << "layer=" << QString::fromStdString(layer_id);
                SidescanLoadResult err;
                err.layer_id = layer_id; err.generation = gen; err.load_failed = true;
                return err;
            } catch (...) {
                qWarning() << "SidescanViewController BG: unknown exception"
                           << "layer=" << QString::fromStdString(layer_id);
                SidescanLoadResult err;
                err.layer_id = layer_id; err.generation = gen; err.load_failed = true;
                return err;
            }
        });

    emit loadingStarted();
    watcher->setFuture(future);
}

} // namespace dolphin::ui
