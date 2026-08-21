#pragma once

#include "core/SidescanPing.h"

#include <vector>

namespace dolphin::pipeline::radiometry {

inline constexpr uint64_t kAlgorithmRevision = 1;

enum class AgcMode { Global = 0, Variable = 1 };
enum class AgcSmoothingType { Mean = 0, Median = 1 };

struct TvgSettings {
    bool enabled = false;
    float spreading = 20.f;
    float absorption = 0.f;
    float fallback_blanking_m = 1.f;
};

struct ArcSettings {
    bool enabled = false;
    float exponent = 1.5f;
    float gain_cap_db = 12.f;
};

struct AgcSettings {
    bool enabled = false;
    AgcMode mode = AgcMode::Global;
    float strength = 0.5f;
    int along_track_win = 50;
    AgcSmoothingType smoothing_type = AgcSmoothingType::Mean;
    int smoothing_win = 5;
    int edge_skip_samples = 50;
    float noise_floor_pct = 2.f;
    float gain_cap_db = 24.f;
    float target_mean = 32767.5f;
};

inline bool operator==(const TvgSettings& a, const TvgSettings& b) noexcept
{
    return a.enabled == b.enabled && a.spreading == b.spreading
        && a.absorption == b.absorption
        && a.fallback_blanking_m == b.fallback_blanking_m;
}
inline bool operator!=(const TvgSettings& a, const TvgSettings& b) noexcept { return !(a == b); }

inline bool operator==(const ArcSettings& a, const ArcSettings& b) noexcept
{
    return a.enabled == b.enabled && a.exponent == b.exponent
        && a.gain_cap_db == b.gain_cap_db;
}
inline bool operator!=(const ArcSettings& a, const ArcSettings& b) noexcept { return !(a == b); }

inline bool operator==(const AgcSettings& a, const AgcSettings& b) noexcept
{
    return a.enabled == b.enabled && a.mode == b.mode && a.strength == b.strength
        && a.along_track_win == b.along_track_win
        && a.smoothing_type == b.smoothing_type
        && a.smoothing_win == b.smoothing_win
        && a.edge_skip_samples == b.edge_skip_samples
        && a.noise_floor_pct == b.noise_floor_pct
        && a.gain_cap_db == b.gain_cap_db && a.target_mean == b.target_mean;
}
inline bool operator!=(const AgcSettings& a, const AgcSettings& b) noexcept { return !(a == b); }

bool canApplyArc(const core::SidescanPing& ping) noexcept;
bool applyTvg(std::vector<core::SidescanPing>& pings, const TvgSettings& settings);
bool applyArc(std::vector<core::SidescanPing>& pings, const ArcSettings& settings);
bool applyAgc(std::vector<core::SidescanPing>& pings, const AgcSettings& settings);

} // namespace dolphin::pipeline::radiometry
