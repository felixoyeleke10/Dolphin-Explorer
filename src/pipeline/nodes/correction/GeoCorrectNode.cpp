#include "pipeline/nodes/correction/GeoCorrectNode.h"
#include "core/SidescanPing.h"
#include "geo/GeoUtils.h"
#include <cmath>

namespace dolphin::pipeline {

static constexpr double DEG_TO_RAD = M_PI / 180.0;
NodeSchema GeoCorrectNode::schema() const
{
    return NodeSchema{
        "geocorrect", "Geo-Correction", "Correction",
        {
            {"layback_m", {"Layback (m)", 0.0f, 0.0f, 1000.0f}},
        }
    };
}

ArtifactBuffer GeoCorrectNode::process(const ArtifactBuffer& input,
                                        const NodeParams& params) const
{
    float layback_m = 0.0f;
    if (params.count("layback_m"))
        layback_m = std::get<float>(params.at("layback_m"));

    ArtifactBuffer output = input;

    for (auto& artifact : output) {
        auto* ping = std::get_if<core::SidescanPing>(&artifact);
        if (!ping || !ping->nav.valid) continue;

        double heading_rad = ping->nav.heading_deg * DEG_TO_RAD;

        if (layback_m > 0.f) {
            const double east_m  = -static_cast<double>(layback_m) * std::sin(heading_rad);
            const double north_m = -static_cast<double>(layback_m) * std::cos(heading_rad);
            double corrected_lon = 0.0;
            double corrected_lat = 0.0;
            if (geo::offsetNavByGroundMetres(
                    ping->nav, east_m, north_m, corrected_lon, corrected_lat)) {
                ping->nav.lon = corrected_lon;
                ping->nav.lat = corrected_lat;
            }
        }

        ping->correction_flags |= core::CorrectionFlag::GeoCorrect;

        // TODO (Phase 2 — mosaicing): project each sample's slant range onto the
        // ground plane using perp_heading and the slant-range correction already
        // applied by SlantRangeNode.  This requires storing a per-ping bearing
        // field on SidescanPing so the render layer can geo-reference footprints.
        // For now, only the nav position (layback correction above) is adjusted.
        (void)heading_rad;
    }

    return output;
}

} // namespace dolphin::pipeline
