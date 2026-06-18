// SidescanMapLoadTask.cpp — activateLayer: background ping load, coverage build,
//                           preview raster, and finished-handler wiring.
//   Types: QualityParams, SidescanLoadResult → SidescanMapLoadParams.h
#include <QDebug>
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanMapLoadParams.h"
#include "ui/shared/CoordFormat.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "ui/features/map/sidescan/SidescanRasterCache.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "app/services/ImportService.h"
#include "app/tasks/OperationManager.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "app/display/NavCorrection.h"
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
                                           app::Project*      project,
                                           bool               as_active)
{
    // as_active=false: load this line's raster onto the map as part of the survey
    // overview WITHOUT making it the selected layer (no active-layer state, no
    // viewport centring, no status-bar takeover). Used on project open to show
    // every cached line's raster, not just the selected one.
    if (as_active) m_active_layer_id = layer_id;
    m_project         = project;

    // Off quality: show nav track only, no ping I/O.
    if (m_quality == MapSonarQuality::Off) {
        if (as_active && m_map_view) m_map_view->setActiveLayer(layer_id);
        if (as_active && m_status_ping) m_status_ping->setText(tr("Map sonar off"));
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
        if (as_active && m_status_ping) m_status_ping->setText(tr("Layer not yet indexed"));
        emit loadingFinished();
        return;
    }
    if (layer->sidescanCount() == 0) {
        if (as_active && m_status_ping)
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

    // Progressive load: the heavy tiers (Medium/High) paint a fast Low preview
    // first, then upgrade to the requested tier in the background (prebuildTier →
    // prebuildTierComplete swap). CoverageOnly/Low build directly, so the common
    // default path is unchanged — only the slow tiers stage.
    MapSonarQuality build_quality = m_quality;
    bool            stage_upgrade = false;
    if (m_quality == MapSonarQuality::Medium || m_quality == MapSonarQuality::High) {
        build_quality = MapSonarQuality::Low;
        stage_upgrade = true;
    }
    const QualityParams qp = paramsForQuality(build_quality);

    const int palette_idx = m_palette_idx;

    // Display-time nav corrections live on the layer (model-owned). Captured here
    // and applied in the background task — the SAME correction the waterfall uses.
    const NavProcessingParams nav_params = layer->nav_state;

    if (as_active && m_status_ping) m_status_ping->setText(tr("Loading…"));

    // Active line: mark it active and pre-fit the viewport from the index nav
    // extent so the map centres on it before the background ping load completes.
    // Non-active overview lines skip this — they accumulate on the map and the
    // open-time survey framing (requestFrameSurvey) fits the combined extent.
    if (as_active && m_map_view) {
        m_map_view->setActiveLayer(layer_id);
        const auto ext = idx.navExtent();
        if (ext.valid && !m_map_view->userInteracted()) {
            const bool proj = apply_layer_crs
                              || (!layer_src_ref.empty()
                                  && core::spatialRefIsProjected(layer_src_ref));
            m_map_view->fitToExtent(ext.lon_min, ext.lon_max,
                                    ext.lat_min, ext.lat_max, proj);
        }
    }

    // -- Guards: need an artifact store, import service, and op manager --------
    if (store_path.empty()) {
        if (as_active && m_status_ping)
            m_status_ping->setText(tr("Layer has no artifact store — reimport required"));
        emit loadingFinished();
        return;
    }
    app::ImportService* svc = m_import_service;
    if (!svc) {
        if (as_active && m_status_ping) m_status_ping->setText(tr("Import service unavailable"));
        emit loadingFinished();
        return;
    }
    if (!m_op_mgr) { emit loadingFinished(); return; }

    SssGeorefParams       georef_params   = m_georef_params;
    georef_params.slant_range_corrected   = layer->slant_range_corrected;
    const MapSonarQuality current_quality = build_quality;

    // Raster-first cache: a fresh persisted raster lets the background task skip
    // ping decode + rasterization entirely (parse once, then work from the raster).
    // Keyed on store fingerprint + nav params + slant-range + quality tier; the
    // image is recoloured from the persisted intensity grid on load (palette is
    // not part of the key).
    const rastercache::Meta cache_meta = rastercache::makeMeta(
        store_path, nav_params, layer->slant_range_corrected, build_quality,
        display_ref.id);
    const std::string cache_path =
        rastercache::cachePath(store_path, layer_id, build_quality);

    // Apply the result on the main thread (success path). Stale/superseded results
    // are dropped by OperationManager (per-layer key), so no generation guard is
    // needed; busy-state and loadingFinished are balanced in on_finally below.
    auto on_done = [this, layer_id, palette_idx, build_quality, as_active](SidescanLoadResult res) {
            if (res.load_failed || res.raw_count == 0) {
                if (as_active && m_status_ping)
                    m_status_ping->setText(
                        tr("Sidescan index OK but no pings loaded — cache may be unreadable"));
                return;
            }

            if (m_map_view) {
                // Copy diagnostics before the move so we can enrich with
                // view state (visibility, fit, paint rect) after placement.
                NavStats stats = res.layer_data.nav_stats;

                // A real amplitude raster was built/loaded (vs. coverage/track only).
                const bool has_raster = !res.layer_data.intensity_cache.empty();

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
                                               [static_cast<int>(build_quality)];
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

                // Recolour from the cached intensity if the current look differs
                // from what the background raster baked in (palette captured at load
                // start; the raster applies no display-param overrides). Skipped in
                // the common case — unchanged palette, no custom gain/contrast — so
                // loads pay no extra LUT pass. Mirrors the repalette success handler.
                {
                    const bool palette_changed = (palette_idx != m_palette_idx);
                    const bool params_active = m_display_params.has_value()
                        && !(*m_display_params == SonarDisplayParams{});
                    const auto ic_it = m_layer_intensity_cache.find(res.layer_id);
                    if ((palette_changed || params_active)
                            && ic_it != m_layer_intensity_cache.end()
                            && ic_it->second.valid())
                        res.layer_data.preview_image = colorizeIntensityCache(
                            ic_it->second, m_display_params, m_palette_idx);
                }
                m_map_view->setLayerMapData(layer_id, std::move(res.layer_data));
                if (layer_id == m_active_layer_id)
                    m_map_view->setActiveLayer(layer_id);
                // Non-active overview line that now has a raster: drop the temporary
                // index nav track so it reads as a swath (like the active line), not
                // a swath-plus-centreline. setLayerMapData preserves the track flag
                // set by showNavTrackFromIndex, so clear it explicitly here.
                if (!as_active && has_raster)
                    m_map_view->setNavTrackVisible(layer_id, false);
                m_loaded_layers.insert(layer_id);
                m_layer_pings_cache[res.layer_id] = std::move(res.map_pings_cache);

                if (as_active) emit loadingProgress(100);   // data placed → complete

                // Populate stage-2/3 diagnostics from the now-live map view.
                stats.layer_visible  = m_map_view->isLayerVisible(layer_id);
                stats.layer_active   = (layer_id == m_active_layer_id);
                stats.fit_applied    = !m_map_view->userInteracted();

                const QRectF pr      = m_map_view->layerPaintRect(layer_id);
                stats.paint_rect     = pr.toRect();
                stats.paint_onscreen = !pr.isEmpty()
                    && m_map_view->rect().intersects(pr.toRect());

                emit mapDiagnosticsReady(QString::fromStdString(layer_id), stats);

                if (!res.has_sample_nav && as_active && m_status_ping) {
                    const qulonglong total_pings = res.total_ssc_entries / 2;
                    m_status_ping->setText(
                        tr("%1 pings — no valid GPS for map display")
                            .arg(total_pings > 0 ? total_pings
                                                 : static_cast<qulonglong>(res.raw_count)));
                    return;
                }
                if (!res.has_sample_nav) return;  // non-active: nothing more to do
            }

            // -- Status bar (active line only) ---------------------------------
            if (!as_active) return;
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
    };

    ++m_active_builds;
    m_data_state = ViewerDataState::Loading;
    emit loadingStarted();

    // Keyed per-layer so a newer load for the SAME layer supersedes this one,
    // while other layers load concurrently (distinct keys). NOT heavy: this is
    // display rasterization of an already-imported layer, not an import/decode
    // job, so it must not sit under the D-14 cap (2) — capping it starved
    // multi-line projects to 2 cores and made parse→display feel slow. The global
    // QThreadPool still bounds total concurrency. Auto-tracked in DiagnosticsHub;
    // on_finally balances the busy counter + loadingFinished on every outcome.
    m_op_mgr->run<SidescanLoadResult>(
        tr("Loading sidescan map — %1").arg(QString::fromStdString(layer_id)),
        [this, as_active, svc, store_path, store_format, idx, source_path,
         layer_src_ref, apply_layer_crs, display_ref, layer_id,
         layer_freq_hz, layer_low_freq_hz, qp, palette_idx,
         georef_params, current_quality, nav_params,
         cache_path, cache_meta](app::CancellationToken cancel)
        -> SidescanLoadResult
        {
            try {
            SidescanLoadResult result;
            result.layer_id   = layer_id;

            // Report 0–100 progress for the ACTIVE layer's build to the status bar
            // (marshalled to the main thread). Non-active overview loads stay silent.
            auto report = [this, as_active](int pct) {
                if (!as_active) return;
                QMetaObject::invokeMethod(this, [this, pct]() {
                    emit loadingProgress(pct);
                }, Qt::QueuedConnection);
            };

            // -- Raster fast path: load the persisted raster, skip ping decode --
            // If a fresh cached raster exists, reconstruct the map data straight
            // from it (no store read, no rasterization). Status-bar summary is
            // persisted alongside so the UI is identical to a freshly-built load.
            {
                rastercache::Summary sum;
                if (rastercache::load(cache_path, cache_meta, result.layer_data, sum)) {
                    result.raw_count          = 1;  // non-zero → not a load failure
                    result.has_sample_nav     = sum.has_sample_nav;
                    result.sample_lat         = sum.sample_lat;
                    result.sample_lon         = sum.sample_lon;
                    result.sample_alt_m       = sum.sample_alt;
                    result.sample_is_proj     = sum.sample_is_proj;
                    result.track_m            = sum.track_m;
                    result.total_ssc_entries  = sum.total_ssc_entries;
                    result.preview_port_count = sum.preview_port_count;
                    result.quality_reduced    = sum.quality_reduced;
                    // Colourise from the persisted intensity grid (palette captured
                    // at load start; on_done re-LUTs if palette/params changed since).
                    if (!result.layer_data.intensity_cache.empty()) {
                        IntensityCache ic;
                        ic.pixels    = result.layer_data.intensity_cache;
                        ic.w         = result.layer_data.intensity_w;
                        ic.h         = result.layer_data.intensity_h;
                        ic.disp_low  = result.layer_data.intensity_disp_low;
                        ic.disp_high = result.layer_data.intensity_disp_high;
                        result.layer_data.preview_image =
                            SidescanViewController::colorizeIntensityCache(
                                ic, std::nullopt, palette_idx);
                    }
                    report(100);   // cache hit: instant
                    return result;
                }
            }
            report(4);   // beginning the full build (cache miss)

            // -- Build bounded preview index -----------------------------------
            core::ArtifactIndex map_idx = idx;

            // For pinned single-band layers, remove the other frequency band.
            if (layer_low_freq_hz == 0.f)
                filterSidescanEntriesByBand(map_idx, layer_freq_hz);

            result.total_ssc_entries =
                map_idx.byType(core::ArtifactType::Sidescan).size();
            const size_t total_groups = result.total_ssc_entries / 2;

            // Thin to the quality-determined ping group cap.
            // max_ping_groups == 0 (High quality) means "use all pings", but
            // if the file exceeds kFullSafeLimit we fall back to Medium params.
            size_t effective_cap = qp.max_ping_groups;
            if (effective_cap == 0) {
                if (total_groups > kFullSafeLimit) {
                    effective_cap = paramsForQuality(MapSonarQuality::Medium).max_ping_groups;
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

            if (cancel.isCancelled()) {
                result.load_failed = true; return result;
            }

            // Reading pings is the bulk of the work — map its 0..1 fraction to 5–60%.
            auto raw = svc->loadAllSidescanPingsFromStore(
                store_path, store_format, map_idx, source_path,
                qp.max_samples_per_ping,
                [&report](float f) { report(5 + static_cast<int>(f * 55.f)); });

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

            if (cancel.isCancelled()) {
                result.load_failed = true; return result;
            }

            // Display-time nav corrections (model-owned) — the SAME correction the
            // waterfall applies via WaterfallView::runNavCorrections, so the map and
            // waterfall agree. No-op when the layer has none; applied to the source
            // nav before normalize/reprojection.
            raw = applySidescanNavCorrections(std::move(raw), nav_params);
            report(66);   // pings read + nav-corrected; reprojecting next

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
            report(80);   // coverage + nav track built; rasterizing next

            // -- Sonar preview image (quality >= Low) --------------------------
            if (qp.max_image_dim > 0 && !cancel.isCancelled()) {
                const bool built = buildSwathPreviewImage(
                    map_pings, result.layer_data,
                    qp.max_image_dim, palette_idx, *cancel.flag(),
                    georef_params, qp.min_strip_cos, qp.cell_budget_div);
                result.quality_reduced = built && result.layer_data.preview_reduced;
            }
            report(98);   // raster done; placing on the map
            // -- Build / CRS fields for diagnostics ---------------------------
            result.layer_data.nav_stats.quality_used    =
                result.quality_reduced ? MapSonarQuality::Medium : current_quality;
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

            // -- Persist the built raster (parse once, reuse forever) ----------
            // Write the intensity grid + coverage + nav track so the next open
            // takes the fast path above instead of decoding pings again. Derived
            // artifact — safe to delete; the fingerprint in cache_meta invalidates
            // it if the store, nav params, or quality tier change.
            if (!cancel.isCancelled()) {
                rastercache::Summary sum;
                sum.has_sample_nav     = result.has_sample_nav;
                sum.sample_lat         = result.sample_lat;
                sum.sample_lon         = result.sample_lon;
                sum.sample_alt         = result.sample_alt_m;
                sum.sample_is_proj     = result.sample_is_proj;
                sum.track_m            = result.track_m;
                sum.total_ssc_entries  = result.total_ssc_entries;
                sum.preview_port_count = result.preview_port_count;
                sum.quality_reduced    = result.quality_reduced;
                rastercache::save(cache_path, cache_meta, sum, result.layer_data);
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
                err.layer_id = layer_id; err.load_failed = true;
                return err;
            } catch (...) {
                qWarning() << "SidescanViewController BG: unknown exception"
                           << "layer=" << QString::fromStdString(layer_id);
                SidescanLoadResult err;
                err.layer_id = layer_id; err.load_failed = true;
                return err;
            }
        },
        std::move(on_done),
        "sss:load:" + layer_id,
        /*heavy=*/false,   // display build, not import/decode — capped via the "map"
                           // lane below, not the D-14 import cap
        [this]() {
            // Every outcome (success / supersede / fail): balance the busy counter
            // and signal so the indicator + import "Loading into map…" stay correct.
            if (m_active_builds > 0) --m_active_builds;
            // Only transition Loading→Ready: don't resurrect a viewer that
            // deactivate() already reset to Idle while a cancelled build's
            // finalizer was still pending.
            if (m_active_builds == 0 && m_data_state == ViewerDataState::Loading)
                m_data_state = ViewerDataState::Ready;
            emit loadingFinished();
        },
        // Dedicated map-build lane (cap 2) so loading many lines doesn't fan out all
        // at once and stays separate from the import/decode ("heavy") lane.
        /*lane=*/"map");

    // Stage 2: upgrade to the full requested tier in the background. When it lands,
    // prebuildTierComplete swaps it in (guarded on still-current quality + layer).
    if (stage_upgrade) prebuildTier(layer_id, m_quality, project);
}

// -- showNavTrackFromIndex -----------------------------------------------------
// Instant survey overview: draw a layer's track from the in-memory artifact index
// (no ping decode, no raster). Reprojects each index nav point to the display CRS.

void SidescanViewController::showNavTrackFromIndex(const std::string& layer_id,
                                                   app::Project*      project)
{
    if (!m_map_view) return;
    auto* layer = project ? project->findLayer(layer_id) : nullptr;
    if (!layer || !layer->index_built || layer->artifact_index.empty()) return;

    // Don't clobber a layer that already has full map data (or any nav track).
    if (m_loaded_layers.count(layer_id)) return;
    if (const auto* ex = m_map_view->layerData(layer_id); ex && !ex->nav_track.empty())
        return;

    const auto* src = project->findSource(layer->source_id);
    core::SpatialRef src_ref = layer->source_spatial_ref;
    if (src_ref.empty() && src) src_ref = src->source_spatial_ref;
    const core::SpatialRef display_ref = project->displaySpatialRef();

    const auto& entries = layer->artifact_index.entries;
    const size_t step   = std::max<size_t>(1, entries.size() / 1000);  // ~1000 points

    LayerMapData ld;
    ld.kind           = LayerMapKind::Track;
    ld.show_nav_track = true;
    ld.is_projected   = false;   // reprojected to the display CRS below
    ld.nav_track.reserve(entries.size() / step + 2);
    for (size_t i = 0; i < entries.size(); i += step) {
        const auto& e = entries[i];
        if (e.lat == 0.0 && e.lon == 0.0) continue;
        core::NavPoint np;
        np.lat          = e.lat;
        np.lon          = e.lon;
        np.is_projected = e.is_projected;
        np.spatial_ref  = src_ref;
        np.valid        = true;
        core::NavPoint out;
        if (!geo::normalizeNavForMap(np, display_ref, out) || !out.valid) continue;
        ld.nav_track.emplace_back(out.lon, out.lat);
        ld.lon_min = std::min(ld.lon_min, out.lon);
        ld.lon_max = std::max(ld.lon_max, out.lon);
        ld.lat_min = std::min(ld.lat_min, out.lat);
        ld.lat_max = std::max(ld.lat_max, out.lat);
    }
    if (ld.nav_track.empty()) return;
    m_map_view->setLayerMapData(layer_id, std::move(ld));
    // setLayerMapData() preserves an existing layer's show_nav_track, so force it on
    // (mirrors the SBP/MAG path) — otherwise the track is built but never rendered.
    m_map_view->setNavTrackVisible(layer_id, true);
}

} // namespace dolphin::ui
