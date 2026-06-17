#pragma once
// Quality-based load parameters and background task result type.
// Shared between SidescanMapLoadTask.cpp and any future load-finish split.
#include "ui/features/map/MapTypes.h"
#include "core/SidescanPing.h"
#include "core/SpatialRef.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dolphin::ui {
namespace detail {

struct QualityParams {
    size_t max_ping_groups;
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

// max_ping_groups == 0 means "no thinning — use all available ping groups".
// For Full quality the background task applies an automatic safety fallback
// if the file has more groups than kFullSafeLimit.
inline QualityParams paramsForQuality(MapSonarQuality q)
{
    // Quality controls resolution only (ping count + image size).
    // Artifact guards are uniform across all tiers so switching quality
    // doesn't change which data is visible — only how detailed it looks.
    //                        pings    samp   img
    // Caps are sized for a map *overview* mosaic, not a full-res waterfall: a map
    // preview gains little from >1k samples/ping or huge ping counts, but the read
    // + rasterise cost scales with all three. High/Full were trimmed (was
    // 25000/2048/2048 and a 40000 Full fallback) so first map load isn't slower
    // than parsing; the waterfall still shows full detail. Full keeps 4096px for an
    // explicit max-quality mosaic but falls back to High sooner on very large files.
    switch (q) {
    case MapSonarQuality::Off:
    case MapSonarQuality::CoverageOnly: return {5000,   16,    0, 0.0, 0};
    case MapSonarQuality::Low:          return {5000,  256,  512, 0.0, 0};
    case MapSonarQuality::Medium:       return {10000, 512, 1024, 0.0, 0};
    case MapSonarQuality::High:         return {16000, 1024, 2048, 0.0, 0};
    case MapSonarQuality::Full:         return {0,       0, 4096, 0.0, 0};
    }
    return {5000, 16, 0, 0.0, 0};
}

// Ping-group limit above which Full quality falls back to High params.
constexpr size_t kFullSafeLimit = 24000;

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

    // Normalized pings kept for palette-only re-rasterization (no disk I/O).
    std::vector<core::SidescanPing> map_pings_cache;
};

} // namespace detail
} // namespace dolphin::ui
