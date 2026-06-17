#pragma once
#include "ui/shell/ViewerWindow.h"
#include "render/sonar/SonarDisplayParams.h"
#include <QObject>
#include <QImage>
#include <QRgb>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "ui/features/map/MapTypes.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "core/SidescanPing.h"

class QLabel;

namespace dolphin::app {
class DataLayer;
class ImportService;
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
    std::vector<uint16_t> pixels;
    int   w = 0, h = 0;
    float disp_low = 0.f, disp_high = 1.f;

    bool valid() const { return !pixels.empty() && w > 0 && h > 0; }
};

// -- Per-quality-tier pre-built result -----------------------------------------
// Populated by prebuildTier() (called from ProcessingWindow).
// Holds everything needed to display a layer at a given quality level without
// any background task — quality changes become an O(pixels) palette lookup.
struct PrebuiltTier {
    std::vector<SwathCoverage> coverage;
    std::vector<QPointF>       nav_track;
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
                           app::ImportService* import_service,
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
    void activateLayer(const std::string& layer_id, app::Project* project,
                       bool as_active = true);

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

    // Update heading georef parameters and immediately rebuild the map display.
    void setGeorefParams(const SssGeorefParams& p);

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
    void prebuildTier(const std::string& layer_id,
                      MapSonarQuality    quality,
                      app::Project*      project);

    bool hasCachedTier(const std::string& layer_id, MapSonarQuality quality) const;

    // Build a colored QImage from an IntensityCache without any disk I/O.
    // dp supplies stretch/gain overrides; if dp.display_low == 0 && dp.display_high == 1
    // (identity / not set) the cache's own percentile stretch is used instead.
    // Returns a null QImage when the cache is empty.
    static QImage colorizeIntensityCache(const IntensityCache& cache,
                                         const std::optional<SonarDisplayParams>& dp,
                                         int palette_idx);

signals:
    void contactPicked(double lat, double lon, uint64_t artifact_id, uint32_t sample_idx);
    void loadingStarted();   // emitted when a background map-build task kicks off
    void loadingFinished();  // emitted when the task completes (success, failure, or cancel)
    // Emitted after a successful map build with the per-build diagnostics stats.
    void mapDiagnosticsReady(const QString& layer_id, const dolphin::ui::NavStats& stats);
    // Emitted by prebuildTier() when the background build for one tier completes.
    void prebuildTierComplete(const std::string& layer_id, MapSonarQuality quality);

private:
    MapView*               m_map_view;
    app::ImportService*    m_import_service;
    app::OperationManager* m_op_mgr = nullptr;  // owns per-layer map-build ops (keyed)
    QLabel*             m_status_ping;
    QLabel*             m_status_pos;
    QLabel*             m_status_depth;

    app::Project*         m_project         = nullptr;
    std::string           m_active_layer_id;
    std::set<std::string> m_loaded_layers;   // layers currently on the map

    // Aggregate background-build state for viewerDataState(). m_active_builds
    // counts in-flight activateLayer tasks (layers can load concurrently); the
    // controller reports Loading while any are running and Ready once they end.
    ViewerDataState m_data_state    = ViewerDataState::Idle;
    int             m_active_builds = 0;

    MapSonarQuality  m_quality        = MapSonarQuality::CoverageOnly;
    SssGeorefParams  m_georef_params;
    int              m_palette_idx   = 1;   // PaletteIndex::Greyscale
    std::optional<SonarDisplayParams> m_display_params;  // nullopt = use per-layer auto-stretch

    // Per-layer map builds run through OperationManager keyed "sss:load:<id>",
    // so supersession + cancellation replace the old generation/cancel-flag maps.

    // Normalized pings cached after a successful load.
    // Retained as a fallback for layers that predate the intensity cache.
    std::unordered_map<std::string, std::vector<core::SidescanPing>> m_layer_pings_cache;

    // Per-layer intensity cache at the current quality tier.
    // Used by repaletteAllLayers() for O(pixels) main-thread palette recolors.
    std::unordered_map<std::string, IntensityCache> m_layer_intensity_cache;

    // Pre-built results per layer per quality tier (int key = MapSonarQuality).
    // Populated by prebuildTier(); consumed by setMapSonarQuality() for instant
    // quality switching.
    std::unordered_map<std::string,
        std::unordered_map<int, PrebuiltTier>> m_quality_tier_cache;

    // Apply a pre-built quality tier (from m_quality_tier_cache) to the map with no
    // background work — O(pixels) recolour only. Returns false if no tier is cached
    // for this layer+quality. Shared by setMapSonarQuality() (instant switch) and
    // the staged-upgrade swap (prebuildTierComplete).
    bool applyCachedTier(const std::string& layer_id, MapSonarQuality quality);

    void repaletteAllLayers();
};

} // namespace dolphin::ui
