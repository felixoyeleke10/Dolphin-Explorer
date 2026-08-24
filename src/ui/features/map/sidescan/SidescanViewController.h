#pragma once
#include "ui/shell/ViewerWindow.h"
#include "render/sonar/SonarDisplayParams.h"
#include <QObject>
#include <QImage>
#include <QRgb>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "ui/features/map/MapTypes.h"
#include "ui/features/map/sidescan/SidescanInvalidation.h"
#include "ui/features/map/sidescan/SidescanStagedRefresh.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "core/SidescanPing.h"

class QLabel;

namespace dolphin::app {
class DataLayer;
class Project;
class OperationManager;
}

namespace dolphin::ui {
class MapView;

// -- Intensity cache -----------------------------------------------------------
// Stores the raw per-pixel amplitude from the last successful rasterization so
// the preview image can be rebuilt with a new palette without any disk I/O or
// geometry work.  Sentinel: 0 = transparent (no sonar return).
struct IntensityCache {
    std::shared_ptr<std::vector<uint16_t>> pixels;
    int   w = 0, h = 0;
    float disp_low = 0.f, disp_high = 1.f;

    bool valid() const { return pixels && !pixels->empty() && w > 0 && h > 0; }
};

inline QImage makeGpuIntensityImage(const IntensityCache& cache)
{
    if (!cache.valid()) return {};
    auto* owner = new std::shared_ptr<std::vector<uint16_t>>(cache.pixels);
    return QImage(reinterpret_cast<uchar*>((*owner)->data()),
                  cache.w, cache.h, cache.w * static_cast<int>(sizeof(uint16_t)),
                  QImage::Format_Grayscale16,
                  [](void* info) {
                      delete static_cast<std::shared_ptr<std::vector<uint16_t>>*>(info);
                  }, owner);
}

inline SonarDisplayParams effectiveGpuDisplayParams(
    const IntensityCache& cache,
    const std::optional<SonarDisplayParams>& display,
    bool auto_stretch_enabled)
{
    SonarDisplayParams params = display.value_or(SonarDisplayParams{});
    if (auto_stretch_enabled
            && (!display.has_value()
                || (params.display_low == 0.f && params.display_high == 1.f))) {
        params.display_low = cache.disp_low;
        params.display_high = cache.disp_high;
    }
    return params;
}

// -- Per-quality-tier pre-built result -----------------------------------------
// Produced by prebuildTier() as a short-lived main-thread handoff. The persisted
// raster remains the durable tier cache; this object is consumed when displayed.
struct PrebuiltTier {
    std::vector<SwathCoverage> coverage;
    std::vector<SwathCoverage> coverage_nadir_hidden;
    std::vector<SidescanBeamRay> beam_rays;
    std::vector<QPointF>       nav_track;
    std::vector<QLineF>        raster_boundary;
    double lon_min =  1e18, lon_max = -1e18;
    double lat_min =  1e18, lat_max = -1e18;
    bool   is_projected   = false;
    bool   preview_reduced = false;
    NavStats nav_stats;
    IntensityCache intensity;
};

// Loads sidescan pings and pushes them to the map view.
// All sidescan layers accumulate on the map; they are removed individually.
// Map sonar preview quality is controlled via setMapSonarQuality().
class SidescanViewController : public QObject, public IViewerWindow {
    Q_OBJECT
public:
    SidescanViewController(MapView*            map_view,
                           QLabel*             status_ping,
                           QLabel*             status_pos,
                           QLabel*             status_depth,
                           QObject*            parent = nullptr);

    // Inject the shared OperationManager (owns per-layer map-build ops, keyed
    // "sss:load:<layer-id>"). Set once, after construction, by MainWindow.
    void setOperationManager(app::OperationManager* m) { m_op_mgr = m; }

    // Load a layer onto the map (additive – does not remove other layers).
    // as_active=true (default): make it the selected layer (active state, viewport
    // centring, status bar). as_active=false: load its raster as part of the survey
    // overview without taking selection — used on open to show every cached line's
    // raster, not just the active one. Cache-first either way (no ping decode on hit).
    // cache_only=true (project open, D-06): display persisted work ONLY — load the
    // best already-fresh raster tier at or below the current quality, and if none
    // exists leave the line as its nav track. Never decodes pings, never
    // rasterizes, never stages background tier upgrades. The mosaic builds when
    // the OPERATOR acts: selecting the line, Apply, or changing quality.
    // Returns false ONLY when a cache_only call found no persisted raster and
    // deferred to the operator (callers use it to count/announce deferred
    // lines); all other paths return true.
    bool activateLayer(const std::string& layer_id, app::Project* project,
                       bool as_active = true, bool cache_only = false);

    // Draw a layer's nav track straight from the in-memory artifact index (no ping
    // I/O, no raster) for an instant survey overview. No-op if the layer is already
    // fully loaded. Reprojects index nav to the map's display CRS.
    void showNavTrackFromIndex(const std::string& layer_id, app::Project* project);

    // Remove one layer's swath from the map.
    void unloadLayer(const std::string& layer_id);

    // clear_map=true: remove all swath and reset; false: just clear status labels.
    void deactivate(bool clear_map = false);

    // Change the map sonar preview quality.
    // Cancels any in-progress build and re-activates the current layer at the new tier.
    void setMapSonarQuality(MapSonarQuality quality);

    MapSonarQuality mapSonarQuality() const { return m_quality; }

    // Current global map palette index (the controller computes the initial value from
    // settings; DisplayStateManager owns it thereafter). Used to seed the manager.
    int paletteIndex() const { return m_palette_idx; }

    // Update heading georef parameters and immediately rebuild the map display.
    // Preserves the operator's show_nadir preference (owned here, persisted to
    // QSettings) regardless of the incoming struct's value.
    void setGeorefParams(const SssGeorefParams& p);

    // Operator toggle (Views ▸ SSS "Show nadir band"): display or hide the
    // near-nadir seabed band. Persists to QSettings; the caller triggers the
    // rebuild of loaded lines (this changes the raster fingerprint).
    void setShowNadir(bool show);
    bool showNadir() const { return m_georef_params.show_nadir; }

    // Evict the current layer from the loaded cache and rebuild the map display.
    void reloadCurrentLayer();

    // Evict a single specific layer and reload just that one.
    void reloadLayer(const std::string& layer_id);

    // Persist palette index to QSettings and rebuild all loaded layers so the
    // map immediately reflects the new colour scheme.
    void setPaletteIndex(int idx);

    // Apply display parameters (stretch, gain, contrast) globally to all loaded
    // layers.  Triggers an O(pixels) LUT recolor — no disk I/O.
    // display_low/high from dp override the per-layer auto-stretch unless they
    // are at the identity defaults (0.0 / 1.0), in which case the cache values
    // computed at rasterization time are used instead.
    void setDisplayParams(const SonarDisplayParams& dp);

    // Apply the process-wide auto-stretch preference to every resident map
    // raster. This is an O(pixels) LUT recolour only; geometry and disk caches
    // remain untouched.
    void setAutoStretchEnabled(bool enabled);

    // IViewerWindow
    void onViewerRefresh(ViewerRefreshReason reason,
                         const std::string& layer_id = {}) override;
    // Reports Loading while any background map build is in flight so
    // anyViewerBusy() / refreshLoadingIndicator() keep the loading indicator up
    // until the map is actually built — not just until disk parsing finishes.
    ViewerDataState viewerDataState() const override { return m_data_state; }

    // -- ProcessingWindow API --------------------------------------------------
    // Build and cache a single quality tier for a layer without displaying it.
    // Safe to call for any quality, including the currently active one.
    // Emits prebuildTierComplete when the background task finishes.
    // lane selects the OperationManager concurrency lane: the default "map" lane
    // (cap 2) for background staged upgrades so they don't fan out; an explicit
    // user Apply passes a wider lane so every line rebuilds at once (uses all cores).
    void prebuildTier(const std::string& layer_id,
                      MapSonarQuality    quality,
                      app::Project*      project,
                      const std::string& lane = "map",
                      uint64_t           refresh_generation = 0);

    bool hasCachedTier(const std::string& layer_id, MapSonarQuality quality) const;

    // Current map sonar quality tier (callers use it to know whether a load will
    // stage a background high-tier upgrade).
    MapSonarQuality currentMapQuality() const { return m_quality; }

    // Layers currently on the map (so a batch UI knows which lines a bulk apply will
    // actually rebuild — applyLiveCorrections only touches loaded layers).
    std::vector<std::string> loadedLayers() const {
        return { m_loaded_layers.begin(), m_loaded_layers.end() };
    }

    // Amplitude histogram (sqrt-friendly raw counts) over `nbins` for the Views
    // dynamic-range control, computed from this layer's intensity cache (the map
    // never receives those pixels). Empty if the layer is not rasterised yet.
    std::vector<float> amplitudeHistogram(const std::string& layer_id,
                                          int nbins = 96) const;
    // Auto-stretch black/white points [0,1] the layer was rasterised with, so the
    // dynamic-range handles seat on the effective bounds. Returns false if unknown.
    bool autoStretch(const std::string& layer_id, float& low, float& high) const;

    // Re-rasterize the given layers with their current gain/imaging params. The
    // existing mosaic stays on screen until the new bounded working set is decoded
    // and ready (no blank). Rebuilds line-by-line on a cap-1 lane.
    void applyLiveCorrections(const std::vector<std::string>& layer_ids);

    // Recolour resident intensity rasters for per-layer gain/contrast/stretch
    // changes. No ping decode, georeferencing, or mosaic construction.
    void applyDisplayParams(const std::vector<std::string>& layer_ids);
    // Central invalidation entry point: coalesces repeated requests per layer and
    // selects recolour, background reraster, or authoritative reload.
    void applyInvalidations(const std::vector<SidescanInvalidationRequest>& requests);

    // Build a colored QImage from an IntensityCache without any disk I/O.
    // dp supplies stretch/gain overrides. When auto_stretch_enabled is true,
    // identity display bounds use the cache's canonical line-level stretch;
    // when false, identity means the literal full range [0,1].
    // Returns a null QImage when the cache is empty.
    static QImage colorizeIntensityCache(const IntensityCache& cache,
                                         const std::optional<SonarDisplayParams>& dp,
                                         int palette_idx,
                                         bool auto_stretch_enabled);

signals:
    void contactPicked(double lat, double lon, uint64_t artifact_id, uint32_t sample_idx);
    void loadingStarted(uint64_t task_id, const QString& layer_name);
    void loadingFinished(uint64_t task_id);  // same ID; zero means no async build started
    // 0–100 progress for the ACTIVE layer's map build (drives the status-bar bar).
    // Marshalled to the main thread from the background task.
    void loadingProgress(int percent);
    // Emitted after a successful map build with the per-build diagnostics stats.
    void mapDiagnosticsReady(const QString& layer_id, const dolphin::ui::NavStats& stats);
    void mapLoadFailed(const QString& layer_id, const QString& message);
    // Emitted by prebuildTier() when the background build for one tier completes.
    void prebuildTierComplete(const std::string& layer_id, MapSonarQuality quality);
    // Emitted on EVERY prebuild outcome (success / fail / cancel) — unlike
    // prebuildTierComplete (success only). Lets a progress UI close reliably.
    void prebuildTierFinished(const std::string& layer_id, MapSonarQuality quality);
    // Coarse 0–100 progress for a specific layer's tier build — lets a batch dialog
    // update that line's card (loadingProgress carries no layer id).
    void prebuildTierProgress(const std::string& layer_id, int percent);

private:
    MapView*               m_map_view;
    app::OperationManager* m_op_mgr = nullptr;  // owns per-layer map-build ops (keyed)
    QLabel*             m_status_ping;
    QLabel*             m_status_pos;
    QLabel*             m_status_depth;

    app::Project*         m_project         = nullptr;
    std::string           m_active_layer_id;
    std::set<std::string> m_loaded_layers;   // layers currently on the map
    // Actual tier currently resident in MapView. Without this, every click on an
    // already-loaded Medium/High line launched another disk-cache load/prebuild.
    std::unordered_map<std::string, MapSonarQuality> m_resident_quality;

    // Aggregate background-build state for viewerDataState(). m_active_builds
    // counts in-flight activateLayer tasks (layers can load concurrently); the
    // controller reports Loading while any are running and Ready once they end.
    ViewerDataState m_data_state    = ViewerDataState::Idle;
    int             m_active_builds = 0;
    uint64_t        m_next_load_task_id = 1;

    MapSonarQuality  m_quality        = MapSonarQuality::CoverageOnly;
    SssGeorefParams  m_georef_params;
    int              m_palette_idx   = 1;   // PaletteIndex::Greyscale
    std::optional<SonarDisplayParams> m_display_params;  // nullopt = use per-layer auto-stretch
    bool             m_auto_stretch_enabled = true;

    // Per-layer map builds run through OperationManager keyed "sss:load:<id>",
    // so supersession + cancellation replace the old generation/cancel-flag maps.

    // Per-layer intensity cache at the current quality tier.
    // Used by repaletteAllLayers() for O(pixels) main-thread palette recolors.
    std::unordered_map<std::string, IntensityCache> m_layer_intensity_cache;

    // Short-lived prebuild handoff (int key = MapSonarQuality). The completion
    // signal consumes it synchronously; durable tiers live in the raster cache.
    std::unordered_map<std::string,
        std::unordered_map<int, PrebuiltTier>> m_quality_tier_cache;

    std::unordered_map<std::string, SidescanStagedRefresh> m_staged_refreshes;
    uint64_t m_next_refresh_generation = 1;

    // Apply a pre-built quality tier (from m_quality_tier_cache) to the map with no
    // background work — O(pixels) recolour only. Returns false if no tier is cached
    // for this layer+quality. Shared by setMapSonarQuality() (instant switch) and
    // the staged-upgrade swap (prebuildTierComplete).
    bool applyCachedTier(const std::string& layer_id, MapSonarQuality quality);

    void applyGeometryCorrections(const std::vector<std::string>& layer_ids);
    void handleRefreshTierComplete(const std::string& layer_id,
                                   MapSonarQuality quality,
                                   uint64_t generation);
    void handleRefreshTierFinished(const std::string& layer_id,
                                   MapSonarQuality quality,
                                   uint64_t generation);

    void repaletteAllLayers();
};

} // namespace dolphin::ui
