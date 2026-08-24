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
    starboard.ribbons.push_back(ribbon(0));
    data.coverage = {port, starboard};

    const auto merged = ui::buildSonarDrapeHull(data);
    check(merged.size() == 5, "paired sides produce one hull and separator");
    check(merged.size() > 3 && merged[0].x() == 2 && merged[1].x() == 3,
          "port outer edge is chronological");
    check(merged.size() > 3 && merged[2].x() == 3 && merged[3].x() == 2,
          "starboard outer edge closes the pair");
    check(!merged.empty() && std::isnan(merged.back().x()), "hull ends with separator");

    // A continuity break on only one channel shifts vector indices. Spatial
    // matching must not bridge the first port segment to a distant starboard one.
    ui::SwathCoverage broken_starboard;
    broken_starboard.channel = core::SidescanChannel::Starboard;
    broken_starboard.ribbons.push_back(ribbon(1000));
    broken_starboard.ribbons.push_back(ribbon(0));
    data.coverage = {port, broken_starboard};
    const auto spatially_matched = ui::buildSonarDrapeHull(data);
    check(spatially_matched.size() == 10,
          "unrelated channel segment remains a separate truthful outline");
    check(spatially_matched.size() > 3 && spatially_matched[2].x() == 3,
          "outer hull uses the spatially corresponding starboard segment");

    data.coverage = {port, starboard};
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
