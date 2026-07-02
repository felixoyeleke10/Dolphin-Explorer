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
#include "ui/shared/dialogs/SettingsDialog.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

namespace dolphin::ui {

namespace {

// Returns the maximum preview image dimension for a given quality level.
// CoverageOnly and Off have no image (returns 0).
int maxImageDimForQuality(MapSonarQuality q)
{
    switch (q) {
    case MapSonarQuality::Low:    return 1024;
    case MapSonarQuality::Medium: return 2048;
    case MapSonarQuality::High:   return 4096;
    default:                      return 0;
    }
}

struct PaletteRebuildResult {
    std::string  layer_id;
    uint64_t     generation = 0;
    LayerMapData layer_data;
};

} // namespace

// -- setGeorefParams / reloadCurrentLayer -------------------------------------

void SidescanViewController::setGeorefParams(const SssGeorefParams& p)
{
    m_georef_params = p;
}

void SidescanViewController::reloadCurrentLayer()
{
    if (!m_project) return;
    // Georef params are global — reload every layer currently on the map.
    const std::string saved_active = m_active_layer_id;
    std::vector<std::string> to_reload(m_loaded_layers.begin(), m_loaded_layers.end());
    if (!saved_active.empty()) {
        if (std::find(to_reload.begin(), to_reload.end(), saved_active) == to_reload.end())
            to_reload.push_back(saved_active);
    }
    m_loaded_layers.clear();
    m_layer_pings_cache.clear();      // georef changed — cached pings are stale
    m_layer_raw_pings_cache.clear();  // CRS/georef changed — raw decode is stale
    m_layer_intensity_cache.clear();  // stale at old CRS / georef
    m_quality_tier_cache.clear();     // stale at old CRS / georef
    for (const auto& id : to_reload)
        activateLayer(id, m_project);
    // Restore active-layer selection (activateLayer overwrites m_active_layer_id).
    m_active_layer_id = saved_active;
    if (m_map_view && !saved_active.empty())
        m_map_view->setActiveLayer(saved_active);
}

void SidescanViewController::reloadLayer(const std::string& layer_id)
{
    if (!m_project || layer_id.empty()) return;
    m_loaded_layers.erase(layer_id);
    m_layer_pings_cache.erase(layer_id);
    m_layer_raw_pings_cache.erase(layer_id);
    m_layer_intensity_cache.erase(layer_id);
    m_quality_tier_cache.erase(layer_id);
    activateLayer(layer_id, m_project);
}

void SidescanViewController::applyLiveCorrections(const std::vector<std::string>& layer_ids)
{
    if (!m_project) return;

    // Only rebuild layers that are actually on the map (have a cached tier/pings);
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
    // Process line-by-line: a dedicated "sss:apply" lane with cap 1 rebuilds one
    // line at a time so each completes and lands on the map before the next starts —
    // clear sequential progress ("2 of 4 … 3 of 4"), one ~64 MB raster in flight at
    // a time, and no CPU/IO thrash. Per-line cost is small (decode-free re-Apply via
    // the raw-ping cache + smooth raster sub-progress), so the batch stays snappy.
    if (m_op_mgr) m_op_mgr->setLaneCap("sss:apply", 1);
    for (const auto& layer_id : targets)
        prebuildTier(layer_id, m_quality, m_project, "sss:apply");
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

QImage SidescanViewController::colorizeIntensityCache(
    const IntensityCache& cache, const std::optional<SonarDisplayParams>& dp, int palette_idx)
{
    if (!cache.valid()) return {};

    // When dp is absent OR when display_low/high are at identity defaults (0/1),
    // use the per-layer auto-stretch computed from the data at rasterization time.
    // This lets gain/contrast/palette be applied globally without requiring callers
    // to know the per-layer amplitude range.
    SonarDisplayParams params = dp.value_or(SonarDisplayParams{});
    if (!dp.has_value() ||
        (params.display_low == 0.f && params.display_high == 1.f)) {
        params.display_low  = cache.disp_low;
        params.display_high = cache.disp_high;
    }

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
    const uint16_t* src = cache.pixels.data();
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
    const int max_img_dim = maxImageDimForQuality(m_quality);
    if (max_img_dim == 0) return;   // CoverageOnly / Off: no image to repaint

    for (const auto& layer_id : m_loaded_layers) {
        // -- Fast path: intensity cache present ------------------------------
        // O(pixels) LUT recolor on the main thread — no background task,
        // no disk I/O, no geometry rebuild.
        const auto ic_it = m_layer_intensity_cache.find(layer_id);
        if (ic_it != m_layer_intensity_cache.end() && ic_it->second.valid()) {
            QImage img = colorizeIntensityCache(ic_it->second, m_display_params, m_palette_idx);
            if (!img.isNull() && m_map_view)
                m_map_view->updatePreviewImage(layer_id, std::move(img));
            continue;
        }

        // -- Fallback: pings cached but no intensity cache -------------------
        // Rebuild coverage + image on a background thread (still avoids disk I/O).
        const auto cache_it = m_layer_pings_cache.find(layer_id);
        if (cache_it == m_layer_pings_cache.end()) {
            activateLayer(layer_id, m_project);
            continue;
        }

        if (!m_op_mgr) continue;
        const int             palette_idx = m_palette_idx;
        const SssGeorefParams georef      = m_georef_params;
        const std::vector<core::SidescanPing> pings = cache_it->second;

        // Intentional shared key (same as activateLayer) — safe and load-bearing,
        // NOT a collision risk:
        //  • This fallback only runs for already-loaded layers; an initial load
        //    runs for a not-yet-loaded layer, so the two never race on one layer.
        //  • A reload removes the layer from m_loaded_layers first, so if a
        //    recolour is in flight the reload supersedes it (newest wins — correct).
        //  • unloadLayer / deactivate cancel by this key / "sss:load:" prefix, so
        //    they also cancel an in-flight recolour; a distinct key would let this
        //    on_done write map data after the layer was removed.
        //  • No on_finally here, so supersession never imbalances m_active_builds.
        // Light (no disk I/O), so not heavy.
        m_op_mgr->run<PaletteRebuildResult>(
            tr("Recolouring sidescan map — %1").arg(QString::fromStdString(layer_id)),
            [pings, layer_id, palette_idx, georef, max_img_dim]
            (app::CancellationToken cancel) -> PaletteRebuildResult {
                PaletteRebuildResult result;
                result.layer_id = layer_id;
                for (const auto& p : pings)
                    if (p.nav.valid) { result.layer_data.is_projected = p.nav.is_projected; break; }
                buildSwathNavTrack(pings, result.layer_data);
                buildSwathCoverage(pings, result.layer_data, georef);
                if (!cancel.isCancelled())
                    buildSwathPreviewImage(pings, result.layer_data,
                        max_img_dim, palette_idx, *cancel.flag(), georef, 0.0, 0, false);
                return result;
            },
            [this](PaletteRebuildResult res) {
                if (!m_map_view) return;
                IntensityCache ic;
                ic.pixels    = std::move(res.layer_data.intensity_cache);
                ic.w         = res.layer_data.intensity_w;
                ic.h         = res.layer_data.intensity_h;
                ic.disp_low  = res.layer_data.intensity_disp_low;
                ic.disp_high = res.layer_data.intensity_disp_high;
                if (ic.valid()) {
                    m_layer_intensity_cache[res.layer_id] = std::move(ic);
                    // Re-colorize with current display params (the bg build used
                    // defaults; setDisplayParams may have run while it was in flight).
                    res.layer_data.preview_image = colorizeIntensityCache(
                        m_layer_intensity_cache[res.layer_id],
                        m_display_params, m_palette_idx);
                }
                m_map_view->setLayerMapData(res.layer_id, std::move(res.layer_data));
                if (res.layer_id == m_active_layer_id)
                    m_map_view->setActiveLayer(res.layer_id);
            },
            "sss:load:" + layer_id,
            /*heavy=*/false);
    }
}

// -- unloadLayer / deactivate --------------------------------------------------

void SidescanViewController::unloadLayer(const std::string& layer_id)
{
    if (m_map_view)
        m_map_view->removeLayerData(layer_id);
    m_loaded_layers.erase(layer_id);
    m_layer_pings_cache.erase(layer_id);
    m_layer_raw_pings_cache.erase(layer_id);
    m_layer_intensity_cache.erase(layer_id);
    // Cancel any in-flight build/recolour for this layer; its on_done is then
    // skipped (cancelled) so it can't write map data after removeLayerData.
    if (m_op_mgr) m_op_mgr->cancelByKey("sss:load:" + layer_id);
    if (m_active_layer_id == layer_id)
        m_active_layer_id.clear();
}

void SidescanViewController::deactivate(bool clear_map)
{
    // Cancel all in-flight per-layer builds/recolours; their on_finally still
    // fires (balancing m_active_builds), then reset state explicitly below.
    if (m_op_mgr) m_op_mgr->cancelByPrefix("sss:load:");
    m_layer_pings_cache.clear();
    m_layer_raw_pings_cache.clear();
    m_layer_intensity_cache.clear();
    m_quality_tier_cache.clear();

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
