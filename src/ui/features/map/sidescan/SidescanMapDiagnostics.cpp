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
    case MapSonarQuality::Low:    return 512;
    case MapSonarQuality::Medium: return 1024;
    case MapSonarQuality::High:   return 2048;
    case MapSonarQuality::Full:   return 4096;
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
    m_layer_intensity_cache.erase(layer_id);
    m_quality_tier_cache.erase(layer_id);
    activateLayer(layer_id, m_project);
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

    // When dp is nullopt (no params have been explicitly set), fall back to the
    // per-layer auto-stretch computed at rasterization time.  When dp has a value,
    // honor it as-is — even if display_low/high are 0/1 (identity stretch).
    SonarDisplayParams params = dp.value_or(SonarDisplayParams{});
    if (!dp.has_value()) {
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
    if (m_palette_idx == idx) return;
    m_palette_idx = idx;
    QSettings qs;
    qs.setValue(QStringLiteral("sss/paletteIdx"), idx);
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

        auto& layer_cancel = m_layer_cancel_flags[layer_id];
        if (layer_cancel) layer_cancel->store(true, std::memory_order_relaxed);
        layer_cancel = std::make_shared<std::atomic_bool>(false);
        const uint64_t    gen        = ++m_layer_generations[layer_id];
        auto              cancel     = layer_cancel;
        const int         palette_idx = m_palette_idx;
        const SssGeorefParams georef  = m_georef_params;

        const std::vector<core::SidescanPing> pings = cache_it->second;

        auto* watcher = new QFutureWatcher<PaletteRebuildResult>(this);
        connect(watcher, &QFutureWatcher<PaletteRebuildResult>::finished, this,
            [this, watcher, layer_id]() {
                watcher->deleteLater();
                PaletteRebuildResult res;
                try { res = watcher->result(); } catch (...) { return; }
                const auto it = m_layer_generations.find(res.layer_id);
                if (it == m_layer_generations.end() || res.generation != it->second)
                    return;
                if (m_map_view) {
                    // Extract intensity cache from the rebuild result.
                    IntensityCache ic;
                    ic.pixels    = std::move(res.layer_data.intensity_cache);
                    ic.w         = res.layer_data.intensity_w;
                    ic.h         = res.layer_data.intensity_h;
                    ic.disp_low  = res.layer_data.intensity_disp_low;
                    ic.disp_high = res.layer_data.intensity_disp_high;
                    if (ic.valid()) {
                        m_layer_intensity_cache[res.layer_id] = std::move(ic);
                        // Re-colorize with current display params: the background
                        // build used defaults; setDisplayParams may have been called
                        // while the task was in flight.
                        res.layer_data.preview_image = colorizeIntensityCache(
                            m_layer_intensity_cache[res.layer_id],
                            m_display_params, m_palette_idx);
                    }

                    m_map_view->setLayerMapData(res.layer_id, std::move(res.layer_data));
                    if (res.layer_id == m_active_layer_id)
                        m_map_view->setActiveLayer(res.layer_id);
                }
            });

        QFuture<PaletteRebuildResult> future = QtConcurrent::run(
            [pings, layer_id, gen, cancel, palette_idx, georef, max_img_dim]()
            -> PaletteRebuildResult
            {
                PaletteRebuildResult result;
                result.layer_id   = layer_id;
                result.generation = gen;
                for (const auto& p : pings)
                    if (p.nav.valid) { result.layer_data.is_projected = p.nav.is_projected; break; }
                buildSwathNavTrack(pings, result.layer_data);
                buildSwathCoverage(pings, result.layer_data, georef);
                if (!cancel->load(std::memory_order_relaxed))
                    buildSwathPreviewImage(pings, result.layer_data,
                        max_img_dim, palette_idx, *cancel, georef, 0.0, 0, false);
                return result;
            });
        watcher->setFuture(future);
    }
}

// -- unloadLayer / deactivate --------------------------------------------------

void SidescanViewController::unloadLayer(const std::string& layer_id)
{
    if (m_map_view)
        m_map_view->removeLayerData(layer_id);
    m_loaded_layers.erase(layer_id);
    m_layer_pings_cache.erase(layer_id);
    m_layer_intensity_cache.erase(layer_id);
    // Cancel any in-flight task and erase its generation so the completion
    // handler sees a stale gen and skips writing map data (closes the window
    // between removeLayerData and the next activateLayer increment).
    if (auto it = m_layer_cancel_flags.find(layer_id); it != m_layer_cancel_flags.end()) {
        if (it->second) it->second->store(true, std::memory_order_relaxed);
        m_layer_cancel_flags.erase(it);
    }
    m_layer_generations.erase(layer_id);
    if (m_active_layer_id == layer_id)
        m_active_layer_id.clear();
}

void SidescanViewController::deactivate(bool clear_map)
{
    for (auto& [id, flag] : m_layer_cancel_flags)
        if (flag) flag->store(true, std::memory_order_relaxed);
    m_layer_cancel_flags.clear();
    m_layer_generations.clear();
    m_layer_pings_cache.clear();
    m_layer_intensity_cache.clear();
    m_quality_tier_cache.clear();

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
