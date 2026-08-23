#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolphin::ui {

// Shared map refresh contract. Callers describe what changed; the map controller
// owns the cheapest correct response. Multiple changes for one layer collapse to
// the strongest action, preventing recolour + reraster + reload chains.
enum class SidescanInvalidation : std::uint8_t {
    Appearance, // palette / range / gain: recolour resident intensity only
    Amplitude,  // TVG / AGC / enhancement: reraster without blanking the map
    Geometry,   // SRC / navigation: background reraster, retain old map until swap
    SourceData  // artifact store or index changed: reload from authoritative data
};

enum class SidescanRefreshAction : std::uint8_t {
    Recolour,
    Reraster,
    ProgressiveReraster,
    Reload
};

struct SidescanInvalidationRequest {
    std::string id;
    SidescanInvalidation change = SidescanInvalidation::Appearance;
};

inline SidescanRefreshAction refreshActionFor(SidescanInvalidation change) noexcept
{
    switch (change) {
    case SidescanInvalidation::Appearance: return SidescanRefreshAction::Recolour;
    case SidescanInvalidation::Amplitude:  return SidescanRefreshAction::Reraster;
    case SidescanInvalidation::Geometry:   return SidescanRefreshAction::ProgressiveReraster;
    case SidescanInvalidation::SourceData: return SidescanRefreshAction::Reload;
    }
    return SidescanRefreshAction::Reload;
}

inline std::unordered_map<std::string, SidescanRefreshAction>
coalesceSidescanInvalidations(const std::vector<SidescanInvalidationRequest>& requests)
{
    std::unordered_map<std::string, SidescanRefreshAction> result;
    for (const auto& request : requests) {
        if (request.id.empty()) continue;
        const auto action = refreshActionFor(request.change);
        const auto [it, inserted] = result.emplace(request.id, action);
        if (!inserted && static_cast<unsigned>(action) > static_cast<unsigned>(it->second))
            it->second = action;
    }
    return result;
}

} // namespace dolphin::ui
