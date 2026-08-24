#include "MapDrapeHull.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <deque>

namespace dolphin::ui {

std::vector<QLineF> buildSonarRasterBoundary(const QImage& image,
                                             double x_min, double y_min,
                                             double x_max, double y_max)
{
    const int w = image.width(), h = image.height();
    if (image.isNull() || w <= 0 || h <= 0
            || !(x_min < x_max) || !(y_min < y_max)) return {};
    const auto index = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
    std::vector<uint8_t> valid_mask(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            valid_mask[index(x, y)] = image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
    std::vector<uint8_t> exterior(static_cast<size_t>(w) * h, 0);
    std::deque<QPoint> queue;
    const auto valid = [&](int x, int y) { return valid_mask[index(x, y)] != 0; };
    const auto seed = [&](int x, int y) {
        const size_t i = index(x, y);
        if (!valid(x, y) && !exterior[i]) { exterior[i] = 1; queue.emplace_back(x, y); }
    };
    for (int x = 0; x < w; ++x) { seed(x, 0); if (h > 1) seed(x, h - 1); }
    for (int y = 1; y + 1 < h; ++y) { seed(0, y); if (w > 1) seed(w - 1, y); }
    constexpr int dx[4] = {-1, 1, 0, 0};
    constexpr int dy[4] = {0, 0, -1, 1};
    while (!queue.empty()) {
        const QPoint p = queue.front(); queue.pop_front();
        for (int d = 0; d < 4; ++d) {
            const int nx = p.x() + dx[d], ny = p.y() + dy[d];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            const size_t i = index(nx, ny);
            if (!exterior[i] && !valid(nx, ny)) {
                exterior[i] = 1; queue.emplace_back(nx, ny);
            }
        }
    }

    const double sx = (x_max - x_min) / w;
    const double sy = (y_max - y_min) / h;
    const auto geo = [&](double px, double py) {
        return QPointF(x_min + px * sx, y_max - py * sy);
    };
    const auto outside = [&](int x, int y) {
        return x < 0 || x >= w || y < 0 || y >= h || exterior[index(x, y)] != 0;
    };
    std::vector<QLineF> edges;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!valid(x, y)) continue;
        if (outside(x, y - 1)) edges.emplace_back(geo(x, y), geo(x + 1, y));
        if (outside(x + 1, y)) edges.emplace_back(geo(x + 1, y), geo(x + 1, y + 1));
        if (outside(x, y + 1)) edges.emplace_back(geo(x + 1, y + 1), geo(x, y + 1));
        if (outside(x - 1, y)) edges.emplace_back(geo(x, y + 1), geo(x, y));
    }
    return edges;
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
