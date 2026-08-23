#pragma once
// Quality-based load parameters and background task result type.
// Shared between SidescanMapLoadTask.cpp (main-thread orchestration) and
// SidescanMapLoadTask.Build.cpp (the off-thread raster build).
#include "ui/features/map/MapTypes.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "ui/features/map/sidescan/SidescanRasterCache.h"
#include "core/ArtifactIndex.h"
#include "core/SidescanPing.h"
#include "core/SpatialRef.h"
#include "app/display/NavProcessingParams.h"
#include "app/display/WaterfallParams.h"
#include "app/tasks/CancellationToken.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dolphin::ui {
namespace detail {

struct QualityParams {
    size_t max_ping_groups;
    size_t max_ping_entries;       // hard decoded channel-record bound
    int    max_samples_per_ping;
    int    max_image_dim;          // 0 = no image (CoverageOnly or Off)

    // Artefact-suppression thresholds — both scale with quality so that
    // higher tiers show more data at the cost of turn/edge artefacts.
    //
    // min_strip_cos: cosine of the maximum allowed heading change between two
    //   consecutive strips.  Pairs that exceed this are skipped (turn artefacts).
    //   cos(20°)≈0.940  cos(25°)≈0.906  cos(30°)≈0.866  cos(40°)≈0.766
    //
    // cell_budget_div: max pixel dimension of a single rasterised cell =
    //   min(img_w,img_h) / cell_budget_div.  Larger divisor = tighter cap.
    //   0 = no cap (Full quality — user sees everything).
    double min_strip_cos;
    int    cell_budget_div;
};

struct QualityLoadPlan {
    MapSonarQuality build_quality = MapSonarQuality::Low;
    bool stage_upgrade = false;
};

inline QualityLoadPlan qualityLoadPlan(MapSonarQuality requested,
                                       bool active_layer) noexcept
{
    if (active_layer
            && (requested == MapSonarQuality::Medium
                || requested == MapSonarQuality::High))
        return {MapSonarQuality::Low, true};

    if (!active_layer
            && (requested == MapSonarQuality::Medium
                || requested == MapSonarQuality::High))
        return {MapSonarQuality::Low, false};

    // Low is already the bounded first image tier; Off/CoverageOnly have no image
    // upgrade to stage.
    return {requested, false};
}

// During rasterization each retained sample exists once in the decoded ping and
// once as an expanded georeferenced SssPoint. Keep that combined payload bounded;
// this deliberately accounts for SssPoint::ground_range_m and struct padding.
inline constexpr size_t kHighSampleWorkingSetBytes =
    size_t{4096} * size_t{1024}
    * (sizeof(core::SidescanSample) + sizeof(SssPoint));
static_assert(kHighSampleWorkingSetBytes <= size_t{160} * 1024 * 1024,
              "High map sample working set exceeds its 160 MiB ceiling");

// max_ping_groups == 0 remains available to internal callers as "no thinning";
// shipped map tiers stay resolution-bounded per D-06 (index/visible-first).
inline QualityParams paramsForQuality(MapSonarQuality q)
{
    // Quality controls resolution only (ping count + image size).
    // Artifact guards are uniform across all tiers so switching quality
    // doesn't change which data is visible — only how detailed it looks.
    //                        groups entries samp  img
    // Caps are sized for a map *overview* mosaic, not a full-res waterfall.  The
    // entry cap is a hard backstop for dual-band/malformed groups: even when a
    // ping number contains more than the usual port+starboard pair, decoded sample
    // payload stays bounded. At High, decoded samples plus their expanded
    // georeferenced SssPoints remain at or below 160 MiB before fixed per-ping
    // metadata and the separately bounded 64 MiB image / 32 MiB intensity grid.
    //
    // High remains its own exact product (4096-pixel raster and twice Medium's
    // cross-track samples); it is never relabelled or silently replaced by Medium.
    // The waterfall remains the full-record, sample-for-sample view.
    switch (q) {
    case MapSonarQuality::Off:
    case MapSonarQuality::CoverageOnly: return {1024, 2048,   16,    0, 0.0, 0};
    case MapSonarQuality::Low:          return {1024, 2048,  256, 1024, 0.0, 0};
    case MapSonarQuality::Medium:       return {2048, 4096,  512, 2048, 0.0, 0};
    case MapSonarQuality::High:         return {4096, 4096, 1024, 4096, 0.0, 0};
    }
    return {1024, 2048, 16, 0, 0.0, 0};
}

// Replace the sidescan portion of an index with a deterministic, group-aware
// subset satisfying both quality bounds.  max_ping_groups == 0 and
// max_ping_entries == 0 remain the explicit internal "unbounded" values.
//
// The second bound matters because one timestamp/ping-number group is not
// guaranteed to contain exactly two records (multi-band stores can contain four,
// and corrupt input can contain more).  Re-thinning by group preserves paired
// channels in normal data; the final uniform trim is only a malformed-group
// memory-safety backstop.
inline void boundSidescanIndexForMap(core::ArtifactIndex& index,
                                     const QualityParams& params)
{
    if (params.max_ping_groups == 0 && params.max_ping_entries == 0)
        return;

    size_t group_cap = params.max_ping_groups;
    if (group_cap == 0)
        group_cap = std::max<size_t>(1, params.max_ping_entries);

    std::vector<core::ArtifactIndexEntry> selected =
        thinSidescanEntriesForMap(index, group_cap);

    while (params.max_ping_entries > 0
           && selected.size() > params.max_ping_entries
           && group_cap > 1) {
        const long double ratio = static_cast<long double>(params.max_ping_entries)
                                / static_cast<long double>(selected.size());
        size_t next_cap = std::max<size_t>(1,
            static_cast<size_t>(static_cast<long double>(group_cap) * ratio));
        if (next_cap >= group_cap) next_cap = group_cap - 1;
        group_cap = next_cap;
        selected = thinSidescanEntriesForMap(index, group_cap);
    }

    if (params.max_ping_entries > 0 && selected.size() > params.max_ping_entries) {
        std::vector<core::ArtifactIndexEntry> trimmed;
        trimmed.reserve(params.max_ping_entries);
        if (params.max_ping_entries == 1) {
            trimmed.push_back(selected.front());
        } else {
            for (size_t i = 0; i < params.max_ping_entries; ++i) {
                const size_t at = i * (selected.size() - 1)
                                / (params.max_ping_entries - 1);
                trimmed.push_back(selected[at]);
            }
        }
        selected = std::move(trimmed);
    }

    index.entries.erase(
        std::remove_if(index.entries.begin(), index.entries.end(),
            [](const core::ArtifactIndexEntry& entry) {
                return entry.type == core::ArtifactType::Sidescan;
            }),
        index.entries.end());
    index.entries.insert(index.entries.end(), selected.begin(), selected.end());
}

struct SidescanLoadResult {
    std::string  layer_id;
    uint64_t     generation      = 0;   // matches m_layer_generations[layer_id] at task start

    // Fully built off the UI thread.
    LayerMapData layer_data;
    bool         quality_reduced = false;

    // Pre-computed status bar values.
    bool   has_sample_nav     = false;
    double sample_lat         = 0.0;
    double sample_lon         = 0.0;
    float  sample_alt_m       = 0.0f;
    bool   sample_is_proj     = false;
    double track_m            = 0.0;
    size_t preview_port_count = 0;

    size_t raw_count         = 0;
    size_t total_ssc_entries = 0;
    bool   load_failed       = false;

    // CRS IDs for which no supported transform was found (pseudo-degree fallback used).
    std::vector<core::SpatialRef> unresolved_crs;

    struct DeferredCacheWrite {
        std::string          path;
        rastercache::Meta    meta;
        rastercache::Summary summary;
        LayerMapData         data;
    };
    std::shared_ptr<DeferredCacheWrite> deferred_cache_write;

};

// Immutable snapshot of everything the off-thread build needs — gathered on the
// main thread in activateLayer(), then consumed by buildSidescanLoadResult()
// with no access back to the controller or the model.
struct SssLoadInputs {
    std::string         store_path;
    std::string         store_format;
    core::ArtifactIndex idx;
    std::string         source_path;
    core::SpatialRef    layer_src_ref;
    bool                apply_layer_crs = false;
    core::SpatialRef    display_ref;
    std::string         layer_id;
    float               layer_freq_hz     = 0.f;
    float               layer_low_freq_hz = 0.f;
    QualityParams       qp{};
    int                 palette_idx = 0;
    bool                auto_stretch_enabled = true;
    SssGeorefParams     georef_params;
    MapSonarQuality     current_quality = MapSonarQuality::Low;
    NavProcessingParams nav_params;
    WaterfallParams     sss_params;        // gain/imaging corrections to render on the map
    std::string         cache_path;
    rastercache::Meta   cache_meta;
};

bool buildSidescanMapProducts(
    const std::vector<core::SidescanPing>& pings,
    LayerMapData& data,
    const SssGeorefParams& georef,
    const QualityParams& quality,
    int palette_idx,
    const std::atomic_bool& cancelled,
    const std::function<void(float)>& raster_progress = {},
    float canonical_stretch_low = -1.f,
    float canonical_stretch_high = -1.f,
    bool produce_color_image = true);

// The full off-thread sidescan map build: raster fast-path, bounded preview
// index, ping load + nav correction + reprojection, coverage/track/raster build,
// status pre-compute, and raster persistence. Pure w.r.t. the controller — all
// inputs arrive via `in`; progress is reported through `report` (0–100);
// cancellation via `cancel`. Defined in SidescanMapLoadTask.Build.cpp.
SidescanLoadResult buildSidescanLoadResult(const SssLoadInputs&            in,
                                           const std::function<void(int)>& report,
                                           app::CancellationToken          cancel);

} // namespace detail
} // namespace dolphin::ui
