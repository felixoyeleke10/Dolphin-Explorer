// SidescanMapDiagnostics.cpp — setGeorefParams, reloadCurrentLayer,
//                              setPaletteIndex, setDisplayParams,
//                              repaletteAllLayers, colorizeIntensityCache,
//                              unloadLayer, deactivate.
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "render/sonar/SSSAmplitudeProcessor.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"
#include "app/tasks/OperationManager.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

namespace dolphin::ui {

// -- setGeorefParams / reloadCurrentLayer -------------------------------------

void SidescanViewController::setGeorefParams(const SssGeorefParams& p)
{
    // show_nadir is an operator display preference owned by this controller
    // (persisted via setShowNadir). Correction-dialog applies and {} resets
    // must not silently flip it.
    const bool keep_nadir = m_georef_params.show_nadir;
    m_georef_params = p;
    m_georef_params.show_nadir = keep_nadir;
}

void SidescanViewController::setShowNadir(bool show)
{
    if (m_georef_params.show_nadir == show) return;
    m_georef_params.show_nadir = show;
    QSettings().setValue(QStringLiteral("sss/showNadir"), show);
    if (m_map_view) {
        for (const auto& layer_id : m_loaded_layers)
            m_map_view->setLayerShowNadir(layer_id, show);
    }
}

void SidescanViewController::reloadCurrentLayer()
{
    if (!m_project) return;
    if (m_op_mgr) {
        m_op_mgr->cancelByPrefix("sss:load:");
        m_op_mgr->cancelByPrefix("sss:prebuild:");
    }
    // Georef params are global — reload every layer currently on the map.
    const std::string saved_active = m_active_layer_id;
    std::vector<std::string> to_reload(m_loaded_layers.begin(), m_loaded_layers.end());
    if (!saved_active.empty()) {
        if (std::find(to_reload.begin(), to_reload.end(), saved_active) == to_reload.end())
            to_reload.push_back(saved_active);
    }
    m_loaded_layers.clear();
    m_resident_quality.clear();
    m_layer_intensity_cache.clear();  // stale at old CRS / georef
    m_quality_tier_cache.clear();     // stale at old CRS / georef
    m_staged_refreshes.clear();
    for (const auto& id : to_reload)
        activateLayer(id, m_project, id == saved_active);
    // Restore active-layer selection (activateLayer overwrites m_active_layer_id).
    m_active_layer_id = saved_active;
    if (m_map_view && !saved_active.empty())
        m_map_view->setActiveLayer(saved_active);
}

void SidescanViewController::reloadLayer(const std::string& layer_id)
{
    if (!m_project || layer_id.empty()) return;
    if (m_op_mgr) {
        m_op_mgr->cancelByKey("sss:load:" + layer_id);
        m_op_mgr->cancelByPrefix("sss:prebuild:" + layer_id + ":");
    }
    m_loaded_layers.erase(layer_id);
    m_resident_quality.erase(layer_id);
    m_layer_intensity_cache.erase(layer_id);
    m_quality_tier_cache.erase(layer_id);
    m_staged_refreshes.erase(layer_id);
    activateLayer(layer_id, m_project, layer_id == m_active_layer_id);
}

void SidescanViewController::applyInvalidations(
    const std::vector<SidescanInvalidationRequest>& requests)
{
    const auto plan = coalesceSidescanInvalidations(requests);
    std::vector<std::string> recolour;
    std::vector<std::string> reraster;
    std::vector<std::string> geometry;
    for (const auto& [id, action] : plan) {
        // Non-resident lines consume current model state when selected; doing
        // background work for them violates the visible-first loading contract.
        if (!m_loaded_layers.count(id)) continue;
        switch (action) {
        case SidescanRefreshAction::Recolour: recolour.push_back(id); break;
        case SidescanRefreshAction::Reraster: reraster.push_back(id); break;
        case SidescanRefreshAction::ProgressiveReraster: geometry.push_back(id); break;
        case SidescanRefreshAction::Reload:   reloadLayer(id); break;
        }
    }
    applyDisplayParams(recolour);
    applyLiveCorrections(reraster);
    applyGeometryCorrections(geometry);
}

void SidescanViewController::applyGeometryCorrections(
    const std::vector<std::string>& layer_ids)
{
    if (!m_project) return;
    if (m_op_mgr) m_op_mgr->setLaneCap("sss:apply", 2);

    for (const auto& layer_id : layer_ids) {
        if (!m_loaded_layers.count(layer_id)) continue;
        if (m_op_mgr)
            m_op_mgr->cancelByPrefix("sss:prebuild:" + layer_id + ":");
        const uint64_t generation = m_next_refresh_generation++;
        if (static_cast<int>(m_quality) > static_cast<int>(MapSonarQuality::Low)) {
            m_staged_refreshes[layer_id] = {
                generation, m_quality, MapSonarQuality::Low, true};
            prebuildTier(layer_id, MapSonarQuality::Low,
                         m_project, "sss:apply", generation);
        } else {
            m_staged_refreshes[layer_id] = {
                generation, m_quality, m_quality, false};
            prebuildTier(layer_id, m_quality, m_project, "sss:apply", generation);
        }
    }
}

void SidescanViewController::handleRefreshTierComplete(
    const std::string& layer_id, MapSonarQuality quality, uint64_t generation)
{
    const auto it = m_staged_refreshes.find(layer_id);
    if (it == m_staged_refreshes.end() || it->second.generation != generation)
        return;

    const StagedRefreshStep step = acceptCompletedTier(
        it->second, generation, quality);
    if (step == StagedRefreshStep::ShowPreviewThenBuildTarget) {
        const MapSonarQuality target = it->second.target;
        if (!m_loaded_layers.count(layer_id)) {
            m_staged_refreshes.erase(it);
            return;
        }
        applyCachedTier(layer_id, quality);
        prebuildTier(layer_id, target, m_project, "sss:apply", generation);
        return;
    }

    if (step == StagedRefreshStep::ShowFinal) {
        if (m_loaded_layers.count(layer_id)) applyCachedTier(layer_id, quality);
        m_staged_refreshes.erase(it);
    }
}

void SidescanViewController::handleRefreshTierFinished(
    const std::string& layer_id, MapSonarQuality quality, uint64_t generation)
{
    const auto it = m_staged_refreshes.find(layer_id);
    if (it == m_staged_refreshes.end() || it->second.generation != generation)
        return;
    const StagedRefreshStep step = acceptFailedTier(
        it->second, generation, quality);
    if (step == StagedRefreshStep::FinalFailed) {
        m_staged_refreshes.erase(it);
        return;
    }
    if (step != StagedRefreshStep::BuildTargetAfterPreviewFailure) return;

    const MapSonarQuality target = it->second.target;
    if (m_project && m_loaded_layers.count(layer_id))
        prebuildTier(layer_id, target, m_project, "sss:apply", generation);
    else
        m_staged_refreshes.erase(it);
}

void SidescanViewController::applyLiveCorrections(const std::vector<std::string>& layer_ids)
{
    if (!m_project) return;

    // Only rebuild layers that are actually on the map;
    // others pick up the stored params lazily when first activated.
    std::vector<std::string> targets;
    for (const auto& id : layer_ids)
        if (m_loaded_layers.count(id)) targets.push_back(id);
    if (targets.empty()) return;

    // Rebuild each target's tier with the new gain/imaging params in the BACKGROUND.
    // Critically, nothing clears the map first: the existing mosaic stays on screen
    // at full quality the whole time, and prebuildTierComplete atomically swaps the
    // freshly-corrected tier in when it's ready (applyCachedTier). No blank, no
    // quality downgrade — the data never disappears during Apply.
    //
    // Use the same bounded concurrency as other heavy work (D-14). Two lines can
    // decode/rasterize concurrently without the unbounded fan-out that caused CPU,
    // memory, and disk thrash, while cutting multi-line Apply latency materially.
    if (m_op_mgr) m_op_mgr->setLaneCap("sss:apply", 2);
    for (const auto& layer_id : targets)
        prebuildTier(layer_id, m_quality, m_project, "sss:apply");
}

void SidescanViewController::applyDisplayParams(
    const std::vector<std::string>& layer_ids)
{
    if (!m_project || !m_map_view) return;
    for (const auto& layer_id : layer_ids) {
        const auto cache = m_layer_intensity_cache.find(layer_id);
        const auto* layer = m_project->findLayer(layer_id);
        if (cache == m_layer_intensity_cache.end() || !cache->second.valid() || !layer)
            continue;
        const SonarDisplayParams display =
            static_cast<const SonarDisplayParams&>(layer->sss_display_state.params);
        QImage image = colorizeIntensityCache(
            cache->second, display, m_palette_idx, m_auto_stretch_enabled);
        if (!image.isNull()) {
            const SonarDisplayParams gpu = effectiveGpuDisplayParams(
                cache->second, display, m_auto_stretch_enabled);
            m_map_view->updatePreviewImage(layer_id, std::move(image), &gpu);
        }
    }
}

// -- colorizeIntensityCache ----------------------------------------------------
//
// Static helper: build a colored ARGB32 QImage from a raw intensity cache,
// display params, and a palette index.  O(pixels) — no disk I/O, no geometry.
// Returns a null QImage when the cache is empty.

void SidescanViewController::setDisplayParams(const SonarDisplayParams& dp)
{
    if (m_display_params.has_value() && *m_display_params == dp) return;
    m_display_params = dp;
    m_palette_idx = dp.palette;   // keep m_palette_idx in sync with display_params.palette
    repaletteAllLayers();
}

void SidescanViewController::setAutoStretchEnabled(bool enabled)
{
    if (m_auto_stretch_enabled == enabled) return;
    m_auto_stretch_enabled = enabled;
    repaletteAllLayers();
}

QImage SidescanViewController::colorizeIntensityCache(
    const IntensityCache& cache, const std::optional<SonarDisplayParams>& dp,
    int palette_idx, bool auto_stretch_enabled)
{
    if (!cache.valid()) return {};

    // Identity bounds inherit the canonical line-level stretch only while the
    // application-wide auto-stretch preference is enabled. Explicit non-identity
    // bounds always win. This keeps map and waterfall semantics identical.
    const SonarDisplayParams params = effectiveGpuDisplayParams(
        cache, dp, auto_stretch_enabled);

    // Build a 65 536-entry uint16 → QRgb LUT (same path as SwathRasterizer).
    std::array<QRgb, 65536> lut;
    for (int i = 0; i < 65536; ++i) {
        const float intensity = SSSAmplitudeProcessor::displayIntensity(
            static_cast<uint16_t>(i), params);
        lut[static_cast<size_t>(i)] = SSSPalette::color(intensity, palette_idx);
    }

    QImage img(cache.w, cache.h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QRgb*           dst = reinterpret_cast<QRgb*>(img.bits());
    const uint16_t* src = cache.pixels->data();
    const int       n   = cache.w * cache.h;

    for (int i = 0; i < n; ++i) {
        if (src[i] > 0) {
            // src[i] is stored as amplitude+1 so subtract 1 before LUT lookup.
            dst[i] = lut[static_cast<size_t>(src[i] - 1)];
        }
        // else: stays transparent (Qt::transparent == 0x00000000)
    }
    return img;
}

// -- amplitudeHistogram / autoStretch ------------------------------------------

std::vector<float> SidescanViewController::amplitudeHistogram(
    const std::string& layer_id, int nbins) const
{
    const auto it = m_layer_intensity_cache.find(layer_id);
    if (it == m_layer_intensity_cache.end() || !it->second.valid() || nbins <= 0)
        return {};
    const auto& px = *it->second.pixels;  // uint16, stored amplitude+1; 0 = no return
    const size_t n = px.size();
    // Sample to a fixed budget so layer selection stays instant on High tiers.
    const size_t stride = std::max<size_t>(1, n / 200000);
    std::vector<float> bins(static_cast<size_t>(nbins), 0.f);
    for (size_t i = 0; i < n; i += stride) {
        const uint16_t v = px[i];
        if (v == 0) continue;
        const float norm = static_cast<float>(v - 1) / 65535.f;
        int b = static_cast<int>(norm * nbins);
        b = std::clamp(b, 0, nbins - 1);
        bins[static_cast<size_t>(b)] += 1.f;
    }
    return bins;
}

bool SidescanViewController::autoStretch(const std::string& layer_id,
                                         float& low, float& high) const
{
    const auto it = m_layer_intensity_cache.find(layer_id);
    if (it == m_layer_intensity_cache.end() || !it->second.valid()) return false;
    low  = m_auto_stretch_enabled ? it->second.disp_low : 0.f;
    high = m_auto_stretch_enabled ? it->second.disp_high : 1.f;
    return true;
}

// -- setPaletteIndex / repaletteAllLayers --------------------------------------

void SidescanViewController::setPaletteIndex(int idx)
{
    // Apply only — DisplayStateManager owns persistence of the global map palette
    // (it writes "sss/paletteIdx" and drives this via the displayStateChanged bus).
    if (m_palette_idx == idx) return;
    m_palette_idx = idx;
    repaletteAllLayers();
}

void SidescanViewController::repaletteAllLayers()
{
    if (!m_project) return;
    if (m_quality == MapSonarQuality::CoverageOnly
            || m_quality == MapSonarQuality::Off)
        return;

    std::vector<std::string> reload_from_raster;
    for (const auto& layer_id : m_loaded_layers) {
        const auto ic_it = m_layer_intensity_cache.find(layer_id);
        if (ic_it != m_layer_intensity_cache.end() && ic_it->second.valid()) {
            QImage img = colorizeIntensityCache(
                ic_it->second, m_display_params, m_palette_idx,
                m_auto_stretch_enabled);
            if (!img.isNull() && m_map_view) {
                const SonarDisplayParams gpu = effectiveGpuDisplayParams(
                    ic_it->second, m_display_params, m_auto_stretch_enabled);
                m_map_view->updatePreviewImage(layer_id, std::move(img), &gpu);
            }
            continue;
        }
        // Legacy/incomplete resident state: leave the old image visible and load
        // the persisted intensity raster again. No decoded-ping retention needed.
        reload_from_raster.push_back(layer_id);
    }

    for (const auto& layer_id : reload_from_raster) {
        m_loaded_layers.erase(layer_id);
        m_resident_quality.erase(layer_id);
        activateLayer(layer_id, m_project, layer_id == m_active_layer_id);
    }
}

// -- unloadLayer / deactivate --------------------------------------------------

void SidescanViewController::unloadLayer(const std::string& layer_id)
{
    if (m_op_mgr) {
        m_op_mgr->cancelByKey("sss:load:" + layer_id);
        m_op_mgr->cancelByPrefix("sss:prebuild:" + layer_id + ":");
    }
    if (m_map_view)
        m_map_view->removeLayerData(layer_id);
    m_loaded_layers.erase(layer_id);
    m_resident_quality.erase(layer_id);
    m_layer_intensity_cache.erase(layer_id);
    m_quality_tier_cache.erase(layer_id);
    m_staged_refreshes.erase(layer_id);
    // Cancel any in-flight build/recolour for this layer; its on_done is then
    // skipped (cancelled) so it can't write map data after removeLayerData.
    if (m_active_layer_id == layer_id)
        m_active_layer_id.clear();
}

void SidescanViewController::deactivate(bool clear_map)
{
    // Cancel all in-flight per-layer builds/recolours; their on_finally still
    // fires (balancing m_active_builds), then reset state explicitly below.
    if (m_op_mgr) {
        m_op_mgr->cancelByPrefix("sss:load:");
        m_op_mgr->cancelByPrefix("sss:prebuild:");
    }
    m_layer_intensity_cache.clear();
    m_quality_tier_cache.clear();
    m_staged_refreshes.clear();
    m_resident_quality.clear();

    m_active_builds = 0;
    m_data_state    = ViewerDataState::Idle;

    m_active_layer_id.clear();
    m_project = nullptr;
    if (clear_map) {
        if (m_map_view) m_map_view->clearAllLayerData();
        m_loaded_layers.clear();
    }
    if (m_status_ping)  m_status_ping->clear();
    if (m_status_pos)   m_status_pos->clear();
    if (m_status_depth) m_status_depth->clear();
}

} // namespace dolphin::ui
