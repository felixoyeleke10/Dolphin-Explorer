#include "MapDrapeHull.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

std::vector<QPointF> buildSonarFootprint(const LayerMapData& data)
{
    const auto& coverage = (!data.show_nadir && !data.coverage_nadir_hidden.empty())
        ? data.coverage_nadir_hidden : data.coverage;
    const QPointF separator{std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN()};
    std::vector<QPointF> footprint;
    for (const auto& channel : coverage) {
        for (const auto& ribbon : channel.ribbons) {
            if (ribbon.size() < 3) continue;
            footprint.insert(footprint.end(), ribbon.begin(), ribbon.end());
            footprint.push_back(separator);
        }
    }
    return footprint;
}

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
    std::vector<bool> used_starboard(starboard ? starboard->ribbons.size() : 0, false);
    const auto appendRibbon = [&](const std::vector<QPointF>& ribbon) {
        if (ribbon.size() < 4) return;
        const size_t half = ribbon.size() / 2;
        for (size_t j = ribbon.size(); j-- > half;) hull.push_back(ribbon[j]);
        for (size_t j = 0; j < half; ++j) hull.push_back(ribbon[j]);
        hull.push_back(separator);
    };
    if (port && starboard && data.show_nadir) {
        for (const auto& port_ribbon : port->ribbons) {
            if (port_ribbon.size() < 4) continue;
            const size_t port_half = port_ribbon.size() / 2;
            const QPointF port_start = port_ribbon.front();
            const QPointF port_end = port_ribbon[port_half - 1];

            // Channels acquire continuity breaks independently.  Never pair by
            // vector index: one missing record would connect unrelated survey
            // sections. Match the closest chronological inner-edge endpoints,
            // then require their separation to fit inside the two swath widths.
            size_t best = starboard->ribbons.size();
            double best_cost = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < starboard->ribbons.size(); ++i) {
                const auto& candidate = starboard->ribbons[i];
                if (used_starboard[i] || candidate.size() < 4) continue;
                const size_t half = candidate.size() / 2;
                const double cost = QLineF(port_start, candidate.front()).length()
                    + QLineF(port_end, candidate[half - 1]).length();
                if (cost < best_cost) { best_cost = cost; best = i; }
            }
            if (best == starboard->ribbons.size()) {
                appendRibbon(port_ribbon);
                continue;
            }
            const auto& starboard_ribbon = starboard->ribbons[best];
            const size_t starboard_half = starboard_ribbon.size() / 2;
            const double endpoint_gap = std::max(
                QLineF(port_start, starboard_ribbon.front()).length(),
                QLineF(port_end, starboard_ribbon[starboard_half - 1]).length());
            const double available_width = std::max(
                QLineF(port_start, port_ribbon.back()).length()
                    + QLineF(starboard_ribbon.front(), starboard_ribbon.back()).length(),
                QLineF(port_end, port_ribbon[port_half]).length()
                    + QLineF(starboard_ribbon[starboard_half - 1],
                             starboard_ribbon[starboard_half]).length());
            if (!std::isfinite(endpoint_gap) || endpoint_gap > available_width * 1.05) {
                appendRibbon(port_ribbon);
                continue;
            }
            used_starboard[best] = true;
            for (size_t j = port_ribbon.size(); j-- > port_half;)
                hull.push_back(port_ribbon[j]);
            for (size_t j = starboard_half; j < starboard_ribbon.size(); ++j)
                hull.push_back(starboard_ribbon[j]);
            hull.push_back(separator);
        }
        for (size_t i = 0; i < starboard->ribbons.size(); ++i)
            if (!used_starboard[i]) appendRibbon(starboard->ribbons[i]);
        return hull;
    }

    const auto appendSide = [&](const SwathCoverage* side) {
        if (!side) return;
        for (const auto& ribbon : side->ribbons) {
            if (ribbon.size() < 4) continue;
            appendRibbon(ribbon);
        }
    };
    appendSide(port);
    appendSide(starboard);
    return hull;
}

} // namespace dolphin::ui
