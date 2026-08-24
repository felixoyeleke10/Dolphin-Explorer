#include "MapDrapeHull.h"

#include <algorithm>
#include <limits>

namespace dolphin::ui {

std::vector<QPointF> buildSonarDrapeHull(const LayerMapData& data)
{
    const auto& coverage = (!data.show_nadir && !data.coverage_nadir_hidden.empty())
        ? data.coverage_nadir_hidden : data.coverage;
    const SwathCoverage* port = nullptr;
    const SwathCoverage* starboard = nullptr;
    for (const auto& item : coverage) {
        if (item.channel == core::SidescanChannel::Port) port = &item;
        else if (item.channel == core::SidescanChannel::Starboard) starboard = &item;
    }

    const QPointF separator{std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN()};
    std::vector<QPointF> hull;
    if (port && starboard && data.show_nadir) {
        const size_t count = std::min(port->ribbons.size(), starboard->ribbons.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& port_ribbon = port->ribbons[i];
            const auto& starboard_ribbon = starboard->ribbons[i];
            if (port_ribbon.size() < 4 || starboard_ribbon.size() < 4) continue;
            const size_t port_half = port_ribbon.size() / 2;
            const size_t starboard_half = starboard_ribbon.size() / 2;
            for (size_t j = port_ribbon.size(); j-- > port_half;)
                hull.push_back(port_ribbon[j]);
            for (size_t j = starboard_half; j < starboard_ribbon.size(); ++j)
                hull.push_back(starboard_ribbon[j]);
            hull.push_back(separator);
        }
        return hull;
    }

    const auto appendSide = [&](const SwathCoverage* side) {
        if (!side) return;
        for (const auto& ribbon : side->ribbons) {
            if (ribbon.size() < 4) continue;
            const size_t half = ribbon.size() / 2;
            for (size_t j = ribbon.size(); j-- > half;)
                hull.push_back(ribbon[j]);
            for (size_t j = 0; j < half; ++j)
                hull.push_back(ribbon[j]);
            hull.push_back(separator);
        }
    };
    appendSide(port);
    appendSide(starboard);
    return hull;
}

} // namespace dolphin::ui
