#include "pipeline/nodes/correction/NavSmoothNode.h"
#include "core/SidescanPing.h"
#include <algorithm>
#include <vector>

namespace dolphin::pipeline {

NodeSchema NavSmoothNode::schema() const
{
    return NodeSchema{
        "nav_smooth", "Nav Smooth", "Correction",
        {
            {"window_pings", {"Window (pings)", 5, 1, 200}},
        }
    };
}

ArtifactBuffer NavSmoothNode::process(const ArtifactBuffer& input,
                                       const NodeParams& params) const
{
    int window = 5;
    if (params.count("window_pings")) window = std::get<int>(params.at("window_pings"));
    window = std::max(1, window);

    // Collect ping pointers and their original positions for smoothing
    ArtifactBuffer output = input;
    std::vector<core::SidescanPing*> pings;
    for (auto& a : output)
        if (auto* p = std::get_if<core::SidescanPing>(&a)) pings.push_back(p);

    const int half = window / 2;
    const int n    = static_cast<int>(pings.size());

    std::vector<double> lats(n), lons(n);
    for (int i = 0; i < n; ++i) { lats[i] = pings[i]->nav.lat; lons[i] = pings[i]->nav.lon; }

    for (int i = 0; i < n; ++i) {
        if (!pings[i]->nav.valid) continue;
        int lo = std::max(0, i - half);
        int hi = std::min(n - 1, i + half);
        double sum_lat = 0, sum_lon = 0;
        int cnt = 0;
        for (int j = lo; j <= hi; ++j) {
            if (!pings[j]->nav.valid) continue;
            sum_lat += lats[j];
            sum_lon += lons[j];
            ++cnt;
        }
        if (cnt > 0) {
            pings[i]->nav.lat = sum_lat / cnt;
            pings[i]->nav.lon = sum_lon / cnt;
            pings[i]->correction_flags |= core::CorrectionFlag::NavSmoothed;
        }
    }

    return output;
}

} // namespace dolphin::pipeline
