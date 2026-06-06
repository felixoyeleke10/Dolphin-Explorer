// SidescanMapQuality.cpp — setMapSonarQuality, prebuildTier, hasCachedTier.
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanMapLoadParams.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "app/services/ImportService.h"
#include "geo/GeoUtils.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"

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

    // Cancel any in-progress background tasks for all layers.
    for (auto& [id, flag] : m_layer_cancel_flags)
        if (flag) flag->store(true, std::memory_order_relaxed);

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

    const std::string saved_active = m_active_layer_id;
    std::vector<std::string> to_reload(m_loaded_layers.begin(), m_loaded_layers.end());
    if (!saved_active.empty()) {
        if (std::find(to_reload.begin(), to_reload.end(), saved_active) == to_reload.end())
            to_reload.push_back(saved_active);
    }
    m_loaded_layers.clear();
    m_layer_intensity_cache.clear();  // stale at the old quality tier

    for (const auto& id : to_reload) {
        // -- Fast path: quality tier already pre-built ---------------------
        const auto tier_map_it = m_quality_tier_cache.find(id);
        if (tier_map_it != m_quality_tier_cache.end()) {
            const auto q_it = tier_map_it->second.find(static_cast<int>(quality));
            if (q_it != tier_map_it->second.end()) {
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
                m_layer_intensity_cache[id] = tier.intensity;
                if (m_map_view) m_map_view->setLayerMapData(id, std::move(ld));
                m_loaded_layers.insert(id);
                continue;
            }
        }
        // -- Fallback: rebuild from disk -----------------------------------
        activateLayer(id, m_project);
    }

    m_active_layer_id = saved_active;
    if (m_map_view && !saved_active.empty())
        m_map_view->setActiveLayer(saved_active);
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
    if (!layer || !layer->index_built || layer->sidescanCount() == 0) return;

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

    auto* watcher = new QFutureWatcher<PrebuildResult>(this);
    connect(watcher, &QFutureWatcher<PrebuildResult>::finished, this,
        [this, watcher]() {
            watcher->deleteLater();
            PrebuildResult res;
            try { res = watcher->result(); } catch (...) { return; }
            if (!res.ok) return;
            m_quality_tier_cache[res.layer_id][static_cast<int>(res.quality)] =
                std::move(res.tier);
            emit prebuildTierComplete(res.layer_id, res.quality);
        });

    watcher->setFuture(QtConcurrent::run(
        [svc, store_path, store_format, idx, source_path,
         layer_src_ref, apply_layer_crs, display_ref, layer_id,
         layer_freq_hz, layer_low_freq_hz, qp, quality, georef]()
        -> PrebuildResult
        {
            try {
            PrebuildResult res;
            res.layer_id = layer_id;
            res.quality  = quality;

            core::ArtifactIndex map_idx = idx;
            // Frequency band filter (same logic as SidescanMapLoadTask).
            if (layer_low_freq_hz == 0.f)
                filterSidescanEntriesByBand(map_idx, layer_freq_hz);

            const size_t total_groups =
                map_idx.byType(core::ArtifactType::Sidescan).size() / 2;

            size_t effective_cap = qp.max_ping_groups;
            if (effective_cap == 0 && total_groups > detail::kFullSafeLimit)
                effective_cap = detail::paramsForQuality(MapSonarQuality::High).max_ping_groups;

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

            if (apply_layer_crs)
                for (auto& ping : raw)
                    if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                        ping.nav.spatial_ref = layer_src_ref;

            auto map_pings = geo::normalizeSidescanPingsForMap(std::move(raw), display_ref);

            LayerMapData ld;
            for (const auto& p : map_pings)
                if (p.nav.valid) { ld.is_projected = p.nav.is_projected; break; }

            buildSwathNavTrack(map_pings, ld);
            buildSwathCoverage(map_pings, ld, georef);

            std::atomic_bool cancelled{false};
            if (qp.max_image_dim > 0)
                buildSwathPreviewImage(map_pings, ld, qp.max_image_dim,
                    0 /* palette 0 = unused — intensity_cache is palette-free */,
                    cancelled, georef, qp.min_strip_cos, qp.cell_budget_div);

            res.tier.coverage        = std::move(ld.coverage);
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
        }));
}

bool SidescanViewController::hasCachedTier(const std::string& layer_id,
                                           MapSonarQuality    quality) const
{
    const auto it = m_quality_tier_cache.find(layer_id);
    if (it == m_quality_tier_cache.end()) return false;
    return it->second.count(static_cast<int>(quality)) > 0;
}

} // namespace dolphin::ui
