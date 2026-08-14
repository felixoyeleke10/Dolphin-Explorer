// SidescanMapQuality.cpp — setMapSonarQuality, prebuildTier, hasCachedTier.
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanMapLoadParams.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "app/services/ImportService.h"
#include "app/tasks/OperationManager.h"
#include "app/display/NavCorrection.h"
#include "ui/shared/processing/SssAmplitudeContext.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "geo/GeoUtils.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "ui/features/map/sidescan/SidescanRasterCache.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------

void SidescanViewController::setMapSonarQuality(MapSonarQuality quality)
{
    if (m_quality == quality) return;
    if (m_op_mgr) {
        m_op_mgr->cancelByPrefix("sss:load:");
        m_op_mgr->cancelByPrefix("sss:prebuild:");
    }
    m_quality_tier_cache.clear();
    m_quality = quality;

    // Any old-tier load/prebuild is now cancelled, and no transient tier result is
    // retained across the switch.

    if (!m_project) return;

    if (quality == MapSonarQuality::Off) {
        emit loadingFinished();
        if (m_map_view) {
            for (const auto& id : m_loaded_layers)
                m_map_view->removeLayerData(id);
            if (!m_active_layer_id.empty())
                m_map_view->setActiveLayer(m_active_layer_id);
        }
        m_loaded_layers.clear();
        m_layer_intensity_cache.clear();
        if (m_status_ping) m_status_ping->setText(tr("Map sonar off"));
        return;
    }

    // Active line first so it renders in the first map-lane slot; the rest follow
    // (the map lane caps concurrency, so order = priority).
    const std::string saved_active = m_active_layer_id;
    std::vector<std::string> to_reload;
    if (!saved_active.empty()) to_reload.push_back(saved_active);
    for (const auto& id : m_loaded_layers)
        if (id != saved_active) to_reload.push_back(id);
    m_loaded_layers.clear();
    m_layer_intensity_cache.clear();  // stale at the old quality tier

    for (const auto& id : to_reload) {
        // Fast path: quality tier already pre-built → instant recolour.
        if (applyCachedTier(id, quality)) {
            m_loaded_layers.insert(id);
            continue;
        }
        // Fallback: rebuild from disk.
        activateLayer(id, m_project);
    }

    m_active_layer_id = saved_active;
    if (m_map_view && !saved_active.empty())
        m_map_view->setActiveLayer(saved_active);
}

// -----------------------------------------------------------------------------
//  applyCachedTier — push a pre-built tier to the map with no background work
// -----------------------------------------------------------------------------

bool SidescanViewController::applyCachedTier(const std::string& layer_id,
                                             MapSonarQuality    quality)
{
    auto tier_map_it = m_quality_tier_cache.find(layer_id);
    if (tier_map_it == m_quality_tier_cache.end()) return false;
    auto q_it = tier_map_it->second.find(static_cast<int>(quality));
    if (q_it == tier_map_it->second.end()) return false;

    // Consume this completed handoff. The persisted raster is the durable tier
    // cache, so retaining a second High intensity grid here only wastes memory.
    PrebuiltTier tier = std::move(q_it->second);
    tier_map_it->second.erase(q_it);
    if (tier_map_it->second.empty()) m_quality_tier_cache.erase(tier_map_it);
    LayerMapData ld;
    ld.coverage        = std::move(tier.coverage);
    ld.beam_rays       = std::move(tier.beam_rays);
    ld.nav_track       = std::move(tier.nav_track);
    ld.lon_min         = tier.lon_min;
    ld.lon_max         = tier.lon_max;
    ld.lat_min         = tier.lat_min;
    ld.lat_max         = tier.lat_max;
    ld.is_projected    = tier.is_projected;
    ld.preview_reduced = tier.preview_reduced;
    ld.nav_stats       = tier.nav_stats;
    ld.preview_image   = colorizeIntensityCache(
        tier.intensity, m_display_params, m_palette_idx,
        m_auto_stretch_enabled);
    m_layer_intensity_cache[layer_id] = std::move(tier.intensity);
    if (m_map_view) m_map_view->setLayerMapData(layer_id, std::move(ld));
    return true;
}

// -----------------------------------------------------------------------------
//  prebuildTier — background-build one quality tier without displaying it
// -----------------------------------------------------------------------------

namespace {

struct PrebuildResult {
    std::string     layer_id;
    MapSonarQuality quality;
    PrebuiltTier    tier;
    bool            ok = false;
};

rastercache::Summary buildTierSummary(
    const std::vector<core::SidescanPing>& map_pings,
    const LayerMapData&                    layer_data,
    size_t                                 total_ssc_entries)
{
    rastercache::Summary summary;
    summary.total_ssc_entries = total_ssc_entries;
    summary.quality_reduced = layer_data.preview_reduced;

    for (const auto& ping : map_pings) {
        if (!ping.nav.valid) continue;
        summary.has_sample_nav = true;
        summary.sample_lat = ping.nav.lat;
        summary.sample_lon = ping.nav.lon;
        summary.sample_alt = ping.nav.altitude_m;
        summary.sample_is_proj = ping.nav.is_projected;
        break;
    }
    // Resolved/fallback nav can produce a valid map track even when the raw
    // ping.nav flag is false. Preserve an honest position in that case too.
    if (!summary.has_sample_nav) {
        for (const QPointF& point : layer_data.nav_track) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())) continue;
            summary.has_sample_nav = true;
            summary.sample_lat = point.y();
            summary.sample_lon = point.x();
            summary.sample_is_proj = layer_data.is_projected;
            break;
        }
    }

    bool have_previous = false;
    core::NavPoint previous;
    for (const auto& ping : map_pings) {
        if (ping.channel != core::SidescanChannel::Port || !ping.nav.valid)
            continue;
        ++summary.preview_port_count;
        if (have_previous)
            summary.track_m += geo::navDistanceMetres(previous, ping.nav);
        previous = ping.nav;
        have_previous = true;
    }
    return summary;
}

} // namespace

void SidescanViewController::prebuildTier(const std::string& layer_id,
                                          MapSonarQuality    quality,
                                          app::Project*      project,
                                          const std::string& lane)
{
    auto* layer = project ? project->findLayer(layer_id) : nullptr;
    if (!layer || !layer->index_built || layer->sidescanCount() == 0) {
        emit prebuildTierFinished(layer_id, quality);  // nothing to build — let UIs close
        return;
    }

    auto* src = project->findSource(layer->source_id);

    const std::string store_path   = layer->artifact_store_path;
    const std::string store_format = layer->artifact_store_format;
    const core::ArtifactIndex idx  = layer->artifact_index;
    const std::string source_path  = src ? src->path : std::string{};
    const float layer_freq_hz      = layer->frequency_hz;
    const float layer_low_freq_hz  = layer->low_frequency_hz;
    core::SpatialRef layer_src_ref = layer->source_spatial_ref;
    if (layer_src_ref.empty() && src) layer_src_ref = src->source_spatial_ref;
    const bool apply_layer_crs =
        layer_src_ref.exact && core::spatialRefIsProjected(layer_src_ref);
    const core::SpatialRef display_ref =
        project ? project->displaySpatialRef() : core::SpatialRef{};
    SssGeorefParams       georef  = m_georef_params;
    georef.slant_range_corrected  = layer->slant_range_corrected;
    const detail::QualityParams qp = detail::paramsForQuality(quality);
    // Display-time nav correction (model-owned) — applied below so the upgraded
    // tier matches activateLayer's first-paint preview (no nav jump on swap).
    const NavProcessingParams nav_params = layer->nav_state;
    const WaterfallParams     sss_params = layer->sss_display_state.params;

    // Raster-first cache for this tier (keyed by the target quality).
    const rastercache::Meta cache_meta = rastercache::makeMeta(
        store_path, nav_params, georef, quality,
        display_ref.id, sss_params);
    const std::string cache_path =
        rastercache::cachePath(store_path, layer_id, quality);

    if (!m_op_mgr) { emit prebuildTierFinished(layer_id, quality); return; }

    // Keyed per layer+tier so a re-request supersedes; runs in the "map" lane
    // (cap 2) below; tracked in DiagnosticsHub via the OperationManager signals.
    const QPointer<SidescanViewController> progress_owner(this);
    m_op_mgr->run<PrebuildResult>(
        tr("Prebuilding sidescan tier — %1").arg(QString::fromStdString(layer_id)),
        [progress_owner, store_path, store_format, idx, source_path,
         layer_src_ref, apply_layer_crs, display_ref, layer_id,
         layer_freq_hz, layer_low_freq_hz, qp, quality, georef, nav_params,
         sss_params, cache_path, cache_meta]
        (app::CancellationToken cancel) -> PrebuildResult
        {
            // Coarse 0–100 progress, marshalled to the main thread. loadingProgress
            // drives the status bar; prebuildTierProgress carries the layer id so a
            // batch dialog can update the right line's card.
            auto report = [progress_owner, layer_id](int pct) {
                if (!progress_owner) return;
                QMetaObject::invokeMethod(progress_owner.data(),
                    [progress_owner, layer_id, pct]() {
                    if (!progress_owner) return;
                    emit progress_owner->loadingProgress(pct);
                    emit progress_owner->prebuildTierProgress(layer_id, pct);
                }, Qt::QueuedConnection);
            };
            report(3);
            try {
            PrebuildResult res;
            res.layer_id = layer_id;
            res.quality  = quality;

            // Raster fast path: reconstruct this tier from the persisted raster.
            {
                rastercache::Summary sum;
                LayerMapData cached;
                if (rastercache::load(cache_path, cache_meta, cached, sum)) {
                    res.tier.coverage        = std::move(cached.coverage);
                    res.tier.beam_rays       = std::move(cached.beam_rays);
                    res.tier.nav_track       = std::move(cached.nav_track);
                    res.tier.lon_min         = cached.lon_min;
                    res.tier.lon_max         = cached.lon_max;
                    res.tier.lat_min         = cached.lat_min;
                    res.tier.lat_max         = cached.lat_max;
                    res.tier.is_projected    = cached.is_projected;
                    res.tier.preview_reduced = cached.preview_reduced;
                    res.tier.nav_stats       = cached.nav_stats;
                    res.tier.intensity.pixels    = std::move(cached.intensity_cache);
                    res.tier.intensity.w         = cached.intensity_w;
                    res.tier.intensity.h         = cached.intensity_h;
                    res.tier.intensity.disp_low  = cached.intensity_disp_low;
                    res.tier.intensity.disp_high = cached.intensity_disp_high;
                    res.ok = true;
                    return res;
                }
            }

            // Build the band-filtered index once. Its unthinned entry count is
            // the operator-facing total persisted in Summary; only the decode
            // copy below is thinned to the requested map tier.
            core::ArtifactIndex map_idx = idx;
            if (layer_low_freq_hz == 0.f)
                filterSidescanEntriesByBand(map_idx, layer_freq_hz);
            const size_t total_ssc_entries =
                map_idx.byType(core::ArtifactType::Sidescan).size();

            // Group count alone is not a hard memory bound: multi-band or
            // malformed groups can contain more than port+starboard. Enforce
            // the decoded channel-record ceiling as well.
            boundSidescanIndexForMap(map_idx, qp);

            std::vector<core::SidescanPing> raw =
                app::ImportService::loadAllSidescanPingsFromStore(
                    store_path, store_format, map_idx, source_path,
                    qp.max_samples_per_ping, {},
                    qp.max_image_dim > 0
                        ? std::function<void(core::SidescanPing&)>{
                            [params = sss_params](core::SidescanPing& ping) {
                                imaging::applyPerPingCalibration(ping, params);
                            }}
                        : std::function<void(core::SidescanPing&)>{},
                    [&cancel]() { return cancel.isCancelled(); });
            if (raw.empty() || cancel.isCancelled()) return res;

            if (apply_layer_crs)
                for (auto& ping : raw)
                    if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                        ping.nav.spatial_ref = layer_src_ref;
            report(55);   // pings decoded — the bulk of the work

            // Same display-time nav correction activateLayer applies (no-op when
            // the layer has none), so the upgraded tier matches the first paint.
            raw = applySidescanNavCorrections(std::move(raw), nav_params);
            if (cancel.isCancelled()) return PrebuildResult{};
            // Finish the same context-dependent stage as first paint. The
            // native-sample per-ping stage ran in the streaming loader before
            // its resolution-only compaction.
            std::shared_ptr<const imaging::SssAmplitudeContext> amplitude_context;
            if (qp.max_image_dim > 0) {
                imaging::SssAmplitudeContextRequest context_request;
                context_request.store_path = store_path;
                context_request.store_format = store_format;
                context_request.artifact_index =
                    std::shared_ptr<const core::ArtifactIndex>(
                        &idx, [](const core::ArtifactIndex*) {});
                context_request.source_path = source_path;
                context_request.frequency_hz = layer_low_freq_hz == 0.f
                    ? layer_freq_hz : 0.f;
                context_request.params = sss_params;
                amplitude_context = imaging::getOrBuildSssAmplitudeContext(
                    context_request, cancel);
                if (amplitude_context)
                    imaging::applySssAmplitudeContext(raw, *amplitude_context);
                else if (!cancel.isCancelled())
                    imaging::applyContextCalibrationAndImaging(raw, sss_params);
            }
            if (cancel.isCancelled()) return PrebuildResult{};
            report(70);   // amplitude-corrected

            auto map_pings = geo::normalizeSidescanPingsForMap(std::move(raw), display_ref);
            if (cancel.isCancelled()) return PrebuildResult{};

            LayerMapData ld;
            for (const auto& p : map_pings)
                if (p.nav.valid) { ld.is_projected = p.nav.is_projected; break; }

            buildSwathNavTrack(map_pings, ld, georef);
            buildSwathCoverage(map_pings, ld, georef);
            report(82);   // coverage built; rasterizing next

            if (cancel.isCancelled()) return PrebuildResult{};
            if (qp.max_image_dim > 0)
                buildSwathPreviewImage(map_pings, ld, qp.max_image_dim,
                    0 /* palette 0 = unused — intensity_cache is palette-free */,
                    *cancel.flag(), georef, qp.min_strip_cos, qp.cell_budget_div,
                    /*ping_lines_only=*/false,
                    // Map the rasterizer's 0–1 onto the card's 82–98 band so the
                    // longest phase shows live sub-progress instead of freezing at 82.
                    [&report](float f) { report(82 + static_cast<int>(f * 16.f)); },
                    amplitude_context ? amplitude_context->stretch_low : -1.f,
                    amplitude_context ? amplitude_context->stretch_high : -1.f);
            report(98);   // raster done

            ld.nav_stats.quality_used = quality;
            ld.nav_stats.pings_available = total_ssc_entries / 2;
            ld.nav_stats.memory_reduced = ld.preview_reduced;

            // Persist this tier's raster so the next open loads it instantly.
            if (!cancel.isCancelled()) {
                const rastercache::Summary sum =
                    buildTierSummary(map_pings, ld, total_ssc_entries);
                rastercache::save(cache_path, cache_meta, sum, ld);
            }

            res.tier.coverage        = std::move(ld.coverage);
            res.tier.beam_rays       = std::move(ld.beam_rays);
            res.tier.nav_track       = std::move(ld.nav_track);
            res.tier.lon_min         = ld.lon_min;
            res.tier.lon_max         = ld.lon_max;
            res.tier.lat_min         = ld.lat_min;
            res.tier.lat_max         = ld.lat_max;
            res.tier.is_projected    = ld.is_projected;
            res.tier.preview_reduced = ld.preview_reduced;
            res.tier.nav_stats       = ld.nav_stats;
            res.tier.intensity.pixels    = std::move(ld.intensity_cache);
            res.tier.intensity.w         = ld.intensity_w;
            res.tier.intensity.h         = ld.intensity_h;
            res.tier.intensity.disp_low  = ld.intensity_disp_low;
            res.tier.intensity.disp_high = ld.intensity_disp_high;
            res.ok = true;
            return res;

            } catch (...) { return PrebuildResult{}; }
        },
        [this](PrebuildResult res) {
            if (!res.ok) return;
            m_quality_tier_cache[res.layer_id][static_cast<int>(res.quality)] =
                std::move(res.tier);
            emit prebuildTierComplete(res.layer_id, res.quality);

            // applyCachedTier consumes a live/current handoff synchronously. If
            // nobody consumed this result (quality changed or layer unloaded), do
            // not retain a full raster indefinitely; it is already persisted.
            const auto layer_it = m_quality_tier_cache.find(res.layer_id);
            if (layer_it != m_quality_tier_cache.end()) {
                layer_it->second.erase(static_cast<int>(res.quality));
                if (layer_it->second.empty())
                    m_quality_tier_cache.erase(layer_it);
            }
        },
        "sss:prebuild:" + layer_id + ":" + std::to_string(static_cast<int>(quality)),
        // By default share the "map" lane (cap 2) with first-paint loads so staged
        // High/Full upgrades don't fan out across every line at once. An explicit
        // user Apply passes a wider lane so all lines rebuild concurrently.
        /*heavy=*/false,
        // on_finally (every outcome) — let a progress UI close reliably even on
        // failure/cancel, where prebuildTierComplete (success-only) never fires.
        [this, layer_id, quality]() { emit prebuildTierFinished(layer_id, quality); },
        lane);
}

bool SidescanViewController::hasCachedTier(const std::string& layer_id,
                                           MapSonarQuality    quality) const
{
    const auto it = m_quality_tier_cache.find(layer_id);
    if (it == m_quality_tier_cache.end()) return false;
    return it->second.count(static_cast<int>(quality)) > 0;
}

} // namespace dolphin::ui
