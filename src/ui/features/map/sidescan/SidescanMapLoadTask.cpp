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
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

using detail::QualityParams;
using detail::SidescanLoadResult;
using detail::paramsForQuality;

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

    const int palette_idx = m_palette_idx;
    const bool auto_stretch_enabled = m_auto_stretch_enabled;

    // Display-time nav corrections live on the layer (model-owned). Captured here
    // and applied in the background task — the SAME correction the waterfall uses.
    const NavProcessingParams nav_params = layer->nav_state;
    // Gain/imaging corrections (model-owned) — applied to the mosaic in the
    // background task so the right-panel SSS tools render on the map.
    const WaterfallParams sss_params = layer->sss_display_state.params;
    SssGeorefParams georef_params = m_georef_params;
    georef_params.slant_range_corrected = layer->slant_range_corrected;

    // Progressive load: the heavy tiers (Medium/High) paint a fast Low preview
    // first, then upgrade to the requested tier in the background (prebuildTier →
    // prebuildTierComplete swap). BUT if the requested tier's raster is already
    // cached fresh on disk (reopening a project, or re-applying the same settings),
    // load it DIRECTLY — no Low preview, no ping decode/re-rasterize. CoverageOnly/
    // Low build directly too, so only an uncached slow tier stages.
    MapSonarQuality build_quality = m_quality;
    bool            stage_upgrade = false;
    if (m_quality == MapSonarQuality::Medium || m_quality == MapSonarQuality::High) {
        const rastercache::Meta full_meta = rastercache::makeMeta(
            store_path, nav_params, georef_params, m_quality,
            display_ref.id, sss_params);
        const std::string full_path =
            rastercache::cachePath(store_path, layer_id, m_quality);
        if (!rastercache::isFresh(full_path, full_meta)) {
            build_quality = MapSonarQuality::Low;
            stage_upgrade = true;
        }
    }
    const QualityParams qp = paramsForQuality(build_quality);

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

    // -- Guards: need an artifact store and operation manager -----------------
    if (store_path.empty()) {
        if (as_active && m_status_ping)
            m_status_ping->setText(tr("Layer has no artifact store — reimport required"));
        emit loadingFinished();
        return;
    }
    if (!m_op_mgr) { emit loadingFinished(); return; }

    const MapSonarQuality current_quality = build_quality;

    // Raster-first cache: a fresh persisted raster lets the background task skip
    // ping decode + rasterization entirely (parse once, then work from the raster).
    // Keyed on store fingerprint + nav params + slant-range + quality tier; the
    // image is recoloured from the persisted intensity grid on load (palette is
    // not part of the key).
    const rastercache::Meta cache_meta = rastercache::makeMeta(
        store_path, nav_params, georef_params, build_quality,
        display_ref.id, sss_params);
    const std::string cache_path =
        rastercache::cachePath(store_path, layer_id, build_quality);

    // Apply the result on the main thread (success path). Stale/superseded results
    // are dropped by OperationManager (per-layer key), so no generation guard is
    // needed; busy-state and loadingFinished are balanced in on_finally below.
    auto on_done = [this, layer_id, palette_idx, auto_stretch_enabled,
                    as_active](SidescanLoadResult res) {
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
                        // The persisted raster is the durable quality-tier cache.
                        // Keep one resident intensity grid for the displayed tier;
                        // duplicating it in m_quality_tier_cache cost another
                        // 32 MiB per High layer with no visual benefit.
                        m_layer_intensity_cache[res.layer_id] = std::move(ic);
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
                    const bool stretch_changed =
                        auto_stretch_enabled != m_auto_stretch_enabled;
                    const auto ic_it = m_layer_intensity_cache.find(res.layer_id);
                    if ((palette_changed || params_active || stretch_changed)
                            && ic_it != m_layer_intensity_cache.end()
                            && ic_it->second.valid())
                        res.layer_data.preview_image = colorizeIntensityCache(
                            ic_it->second, m_display_params, m_palette_idx,
                            m_auto_stretch_enabled);
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
    // Snapshot every field the off-thread build needs into an immutable inputs
    // struct (no access back to the controller or model from the worker).
    detail::SssLoadInputs inputs;
    inputs.store_path        = store_path;
    inputs.store_format      = store_format;
    inputs.idx               = idx;
    inputs.source_path       = source_path;
    inputs.layer_src_ref     = layer_src_ref;
    inputs.apply_layer_crs   = apply_layer_crs;
    inputs.display_ref       = display_ref;
    inputs.layer_id          = layer_id;
    inputs.layer_freq_hz     = layer_freq_hz;
    inputs.layer_low_freq_hz = layer_low_freq_hz;
    inputs.qp                = qp;
    inputs.palette_idx       = palette_idx;
    inputs.auto_stretch_enabled = auto_stretch_enabled;
    inputs.georef_params     = georef_params;
    inputs.current_quality   = current_quality;
    inputs.nav_params        = nav_params;
    inputs.sss_params        = sss_params;
    inputs.cache_path        = cache_path;
    inputs.cache_meta        = cache_meta;

    const QPointer<SidescanViewController> progress_owner(this);
    m_op_mgr->run<SidescanLoadResult>(
        tr("Loading sidescan map — %1").arg(QString::fromStdString(layer_id)),
        [progress_owner, as_active, in = std::move(inputs)](app::CancellationToken cancel)
        -> SidescanLoadResult
        {
            // Report 0–100 progress for the ACTIVE layer's build to the status bar
            // (marshalled to the main thread). Non-active overview loads stay silent.
            auto report = [progress_owner, as_active](int pct) {
                if (!as_active || !progress_owner) return;
                QMetaObject::invokeMethod(progress_owner.data(), [progress_owner, pct]() {
                    if (progress_owner) emit progress_owner->loadingProgress(pct);
                }, Qt::QueuedConnection);
            };
            return detail::buildSidescanLoadResult(in, report, cancel);
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
