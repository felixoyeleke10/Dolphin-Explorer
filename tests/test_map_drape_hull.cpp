#include "ui/features/map/MapDrapeHull.h"

#include <cmath>
#include <iostream>

using namespace dolphin;

namespace {
std::vector<QPointF> ribbon(double base)
{
    return {{base + 0, 0}, {base + 1, 0}, {base + 3, 0}, {base + 2, 0}};
}
}

int main()
{
    int failed = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failed; }
    };

    ui::LayerMapData data;
    ui::SwathCoverage port;
    port.channel = core::SidescanChannel::Port;
    port.ribbons.push_back(ribbon(0));
    ui::SwathCoverage starboard;
    starboard.channel = core::SidescanChannel::Starboard;
    starboard.ribbons.push_back(ribbon(10));
    data.coverage = {port, starboard};

    const auto merged = ui::buildSonarDrapeHull(data);
    check(merged.size() == 5, "paired sides produce one hull and separator");
    check(merged.size() > 3 && merged[0].x() == 2 && merged[1].x() == 3,
          "port outer edge is chronological");
    check(merged.size() > 3 && merged[2].x() == 13 && merged[3].x() == 12,
          "starboard outer edge closes the pair");
    check(!merged.empty() && std::isnan(merged.back().x()), "hull ends with separator");

    data.show_nadir = false;
    data.coverage_nadir_hidden = data.coverage;
    const auto split = ui::buildSonarDrapeHull(data);
    check(split.size() == 10, "hidden nadir keeps sides separate");
    check(split.size() > 4 && std::isnan(split[4].x()), "port segment is terminated");
    check(split.size() > 9 && std::isnan(split[9].x()), "starboard segment is terminated");

    data.coverage_nadir_hidden.clear();
    const auto fallback = ui::buildSonarDrapeHull(data);
    check(fallback.size() == split.size(), "missing alternate coverage uses primary");

    port.ribbons = {{{0, 0}, {1, 0}, {2, 0}}};
    data.coverage = {port};
    data.show_nadir = true;
    check(ui::buildSonarDrapeHull(data).empty(), "undersized ribbons are ignored");

    if (failed == 0) std::cout << "test_map_drape_hull: ALL PASS\n";
    return failed == 0 ? 0 : 1;
}
