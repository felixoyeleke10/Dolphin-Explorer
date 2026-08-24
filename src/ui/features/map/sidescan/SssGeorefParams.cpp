// SssGeorefParams.cpp — composition of the sidescan navigation pipeline.

#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "ui/features/map/sidescan/SssHeadingTable.h"
#include "ui/features/map/sidescan/SssNavRepair.h"
#include "ui/features/map/sidescan/SssNavSmoothing.h"
#include "geo/GeoUtils.h"

#include <cmath>
#include <limits>

namespace dolphin::ui {
namespace {

std::vector<CorrectedSssNav> buildBasePositionTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const SssGeorefParams& params)
{
    std::vector<CorrectedSssNav> table(order.size());
    const double no_heading = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 0; i < order.size(); ++i) {
        const auto position = resolveSssPosition(pings[order[i]], params, no_heading);
        table[i].lat = position.lat;
        table[i].lon = position.lon;
        table[i].valid = position.valid;
        table[i].is_projected = position.is_projected;
        table[i].spatial_ref = position.spatial_ref;
        table[i].flags = position.flags;
    }
    sssnavrepair::repairBoundedRuns(table, pings, order);
    return table;
}

} // namespace

std::vector<CorrectedSssNav> buildCorrectedNavTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const SssGeorefParams& params,
    HeadingStats* out_stats)
{
    const auto base_positions = buildBasePositionTable(pings, order, params);
    const auto headings = sssheading::buildTable(
        pings, order, params, base_positions, out_stats);

    std::vector<CorrectedSssNav> result(order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        result[i].heading_rad = headings[i];
        result[i].heading_valid = !std::isnan(headings[i]);
        const auto position = resolveSssPosition(pings[order[i]], params, headings[i]);
        result[i].lat = position.lat;
        result[i].lon = position.lon;
        result[i].valid = position.valid;
        result[i].is_projected = position.is_projected;
        result[i].spatial_ref = position.spatial_ref;
        result[i].flags = position.flags;
    }
    sssnavrepair::repairBoundedRuns(result, pings, order);
    sssnavsmoothing::apply(result, pings, order,
                           params.smoothing_mode, params.smoothing_window);

    bool have_previous_frame = false;
    bool previous_projected = false;
    double previous_lon = 0.0;
    for (auto& nav : result) {
        if (!nav.valid) continue;
        if (have_previous_frame && nav.is_projected == previous_projected
                && !nav.is_projected)
            nav.lon = geo::unwrapLongitudeNear(nav.lon, previous_lon);
        previous_lon = nav.lon;
        previous_projected = nav.is_projected;
        have_previous_frame = true;
    }
    return result;
}

std::vector<double> buildHeadingTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const SssGeorefParams& params,
    HeadingStats* out_stats)
{
    const auto positions = buildBasePositionTable(pings, order, params);
    return sssheading::buildTable(pings, order, params, positions, out_stats);
}

} // namespace dolphin::ui
