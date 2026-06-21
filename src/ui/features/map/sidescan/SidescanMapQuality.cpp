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
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "geo/GeoUtils.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "ui/features/map/sidescan/SidescanRasterCache.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// -----------------------------------------------------------------------------

void SidescanViewController::setMapSonarQuality(MapSonarQuality quality)
{
    if (m_quality == quality) return;
    m_quality = quality;

    // Cancel any in-progress per-layer builds/recolours (re-launched below at the
    // new quality tier).
    if (m_op_mgr) m_op_mgr->cancelByPrefix("sss:load:");

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
    const auto tier_map_it = m_quality_tier_cache.find(layer_id);
    if (tier_map_it == m_quality_tier_cache.end()) return false;
    const auto q_it = tier_map_it->second.find(static_cast<int>(quality));
    if (q_it == tier_map_it->second.end()) return false;

    const PrebuiltTier& tier = q_it->second;
    LayerMapData ld;
    ld.coverage        = tier.coverage;
    ld.nav_track       = tier.nav_track;
    ld.lon_min         = tier.lon_min;
    ld.lon_max         = tier.lon_max;
    ld.lat_min         = tier.lat_min;
    ld.lat_max         = tier.lat_max;
    ld.is_projected    = tier.is_projected;
    ld.preview_reduced = tier.preview_reduced;
    ld.nav_stats       = tier.nav_stats;
    ld.preview_image   = colorizeIntensityCache(tier.intensity, m_display_params, m_palette_idx);
    m_layer_intensity_cache[layer_id] = tier.intensity;
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

} // namespace

void SidescanViewController::prebuildTier(const std::string& layer_id,
                                          MapSonarQuality    quality,
                                          app::Project*      project)
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
    app::ImportService*   svc     = m_import_service;
    // Display-time nav correction (model-owned) — applied below so the upgraded
    // tier matches activateLayer's first-paint preview (no nav jump on swap).
    const NavProcessingParams nav_params = layer->nav_state;
    const WaterfallParams     sss_params = layer->sss_display_state.params;

    // Raster-first cache for this tier (keyed by the target quality).
    const rastercache::Meta cache_meta = rastercache::makeMeta(
        store_path, nav_params, layer->slant_range_corrected, quality,
        display_ref.id, sss_params);
    const std::string cache_path =
        rastercache::cachePath(store_path, layer_id, quality);

    if (!m_op_mgr) { emit prebuildTierFinished(layer_id, quality); return; }

    // Keyed per layer+tier so a re-request supersedes; runs in the "map" lane
    // (cap 2) below; tracked in DiagnosticsHub via the OperationManager signals.
    m_op_mgr->run<PrebuildResult>(
        tr("Prebuilding sidescan tier — %1").arg(QString::fromStdString(layer_id)),
        [this, svc, store_path, store_format, idx, source_path,
         layer_src_ref, apply_layer_crs, display_ref, layer_id,
         layer_freq_hz, layer_low_freq_hz, qp, quality, georef, nav_params,
         sss_params, cache_path, cache_meta]
        (app::CancellationToken cancel) -> PrebuildResult
        {
            // Coarse 0–100 progress, marshalled to the main thread — drives the
            // execution window's bar (and status bar) through the slow tier build.
            auto report = [this](int pct) {
                QMetaObject::invokeMethod(this, [this, pct]() {
                    emit loadingProgress(pct);
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

            core::ArtifactIndex map_idx = idx;
            // Frequency band filter (same logic as SidescanMapLoadTask).
            if (layer_low_freq_hz == 0.f)
                filterSidescanEntriesByBand(map_idx, layer_freq_hz);

            const size_t total_groups =
                map_idx.byType(core::ArtifactType::Sidescan).size() / 2;

            size_t effective_cap = qp.max_ping_groups;
            if (effective_cap == 0 && total_groups > detail::kFullSafeLimit)
                effective_cap = detail::paramsForQuality(MapSonarQuality::Medium).max_ping_groups;

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

            auto raw = svc->loadAllSidescanPingsFromStore(
                store_path, store_format, map_idx, source_path, qp.max_samples_per_ping);
            if (raw.empty()) return res;
            report(55);   // pings decoded — the bulk of the work

            if (apply_layer_crs)
                for (auto& ping : raw)
                    if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                        ping.nav.spatial_ref = layer_src_ref;

            // Same display-time nav correction activateLayer applies (no-op when
            // the layer has none), so the upgraded tier matches the first paint.
            raw = applySidescanNavCorrections(std::move(raw), nav_params);
            // Same gain/imaging chain as the first-paint build, so the upgraded
            // tier matches (and the SSS tools render on the map). Tiers always
            // build a raster (max_image_dim > 0), so the chain is always relevant.
            if (qp.max_image_dim > 0)
                imaging::applySssMapCorrections(raw, sss_params);
            report(70);   // amplitude-corrected

            auto map_pings = geo::normalizeSidescanPingsForMap(std::move(raw), display_ref);

            LayerMapData ld;
            for (const auto& p : map_pings)
                if (p.nav.valid) { ld.is_projected = p.nav.is_projected; break; }

            buildSwathNavTrack(map_pings, ld);
            buildSwathCoverage(map_pings, ld, georef);
            report(82);   // coverage built; rasterizing next

            if (cancel.isCancelled()) return PrebuildResult{};
            if (qp.max_image_dim > 0)
                buildSwathPreviewImage(map_pings, ld, qp.max_image_dim,
                    0 /* palette 0 = unused — intensity_cache is palette-free */,
                    *cancel.flag(), georef, qp.min_strip_cos, qp.cell_budget_div);
            report(98);   // raster done

            // Persist this tier's raster so the next open loads it instantly.
            if (!cancel.isCancelled()) {
                rastercache::Summary sum;  // tiers carry no status-bar summary
                rastercache::save(cache_path, cache_meta, sum, ld);
            }

            res.tier.coverage        = std::move(ld.coverage);
            res.tier.nav_track       = std::move(ld.nav_track);
            res.tier.lon_min         = ld.lon_min;
            res.tier.lon_max         = ld.lon_max;
            res.tier.lat_min         = ld.lat_min;
            res.tier.lat_max         = ld.lat_max;
            res.tier.is_projected    = ld.is_projected;
            res.tier.preview_reduced = ld.preview_reduced;
            res.tier.nav_stats       = ld.nav_stats;
            res.tier.nav_stats.quality_used = quality;
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
        },
        "sss:prebuild:" + layer_id + ":" + std::to_string(static_cast<int>(quality)),
        // Share the dedicated "map" lane (cap 2) with first-paint loads so staged
        // High/Full upgrades don't fan out across every line at once, and stay
        // separate from the import/decode lane.
        /*heavy=*/false,
        // on_finally (every outcome) — let a progress UI close reliably even on
        // failure/cancel, where prebuildTierComplete (success-only) never fires.
        [this, layer_id, quality]() { emit prebuildTierFinished(layer_id, quality); },
        /*lane=*/"map");
}

bool SidescanViewController::hasCachedTier(const std::string& layer_id,
                                           MapSonarQuality    quality) const
{
    const auto it = m_quality_tier_cache.find(layer_id);
    if (it == m_quality_tier_cache.end()) return false;
    return it->second.count(static_cast<int>(quality)) > 0;
}

} // namespace dolphin::ui
