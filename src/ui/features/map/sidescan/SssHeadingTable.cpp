#include "ui/features/map/sidescan/SssHeadingTable.h"
#include "ui/features/map/sidescan/SssNavContinuity.h"
#include "geo/GeoUtils.h"

#include <cmath>
#include <limits>
#include <numbers>

namespace dolphin::ui::sssheading {
namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kHeadingBlend = 0.2;

template <typename Field>
bool hasNonZero(const std::vector<core::SidescanPing>& pings,
                const std::vector<size_t>& order, Field field)
{
    for (size_t index : order) {
        const float value = field(pings[index].nav);
        if (std::isfinite(value) && value != 0.0f) return true;
    }
    return false;
}

} // namespace

std::vector<double> buildTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const SssGeorefParams& params,
    const std::vector<CorrectedSssNav>& positions,
    HeadingStats* out_stats)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> table(order.size(), kNaN);
    HeadingStats stats;
    if (positions.size() != order.size()) {
        if (out_stats) *out_stats = stats;
        return table;
    }

    const double offset = params.heading_offset_deg * kDegToRad;
    const auto continuity = sssnavcontinuity::deriveThresholds(
        pings, order, positions);
    const bool want_sensor = params.heading_source == SssHeadingSource::Auto
        || params.heading_source == SssHeadingSource::FishSensor;
    const bool want_ship = params.heading_source == SssHeadingSource::Auto
        || params.heading_source == SssHeadingSource::VesselShip;
    const bool want_cog = params.heading_source == SssHeadingSource::Auto
        || params.heading_source == SssHeadingSource::CourseOverGround
        || params.heading_source == SssHeadingSource::SmoothedCourseOverGround;
    const double cog_alpha = params.heading_source == SssHeadingSource::CourseOverGround
        ? 1.0 : kHeadingBlend;

    const bool legacy_has_data = hasNonZero(pings, order,
        [](const core::NavPoint& nav) { return nav.heading_deg; });
    const auto legacyOk = [legacy_has_data](float value) {
        return std::isfinite(value) && (legacy_has_data || value != 0.0f);
    };

    if (want_sensor || want_ship) {
        const bool sensor_has_data = hasNonZero(pings, order,
            [](const core::NavPoint& nav) { return nav.sensor_heading_deg; });
        const bool ship_has_data = hasNonZero(pings, order,
            [](const core::NavPoint& nav) { return nav.ship_heading_deg; });
        const auto sensorOk = [sensor_has_data](float value) {
            return std::isfinite(value) && (sensor_has_data || value != 0.0f);
        };
        const auto shipOk = [ship_has_data](float value) {
            return std::isfinite(value) && (ship_has_data || value != 0.0f);
        };
        for (size_t i = 0; i < order.size(); ++i) {
            const auto& nav = pings[order[i]].nav;
            if (want_sensor && sensorOk(nav.sensor_heading_deg)) {
                table[i] = nav.sensor_heading_deg * kDegToRad + offset;
                ++stats.from_sensor;
            } else if (want_ship && shipOk(nav.ship_heading_deg)) {
                table[i] = nav.ship_heading_deg * kDegToRad + offset;
                ++stats.from_ship;
            }
        }
    }

    if (want_cog) {
        bool have_previous = false;
        size_t previous = 0;
        bool have_cog = false;
        double blended_cog = 0.0;
        for (size_t i = 0; i < order.size(); ++i) {
            if (!positions[i].valid) {
                have_previous = false;
                have_cog = false;
                continue;
            }
            bool new_segment = false;
            if (have_previous) {
                if (!sssnavcontinuity::isContinuousPair(
                        positions[previous], positions[i],
                        pings[order[previous]], pings[order[i]], continuity)) {
                    have_cog = false;
                    new_segment = true;
                } else if (!sssnavcontinuity::positionsCoincide(
                               positions[previous], positions[i])) {
                    const double observed = sssnavcontinuity::headingBetween(
                        positions[previous], positions[i]);
                    blended_cog = have_cog
                        ? geo::blendAngleRad(blended_cog, observed, cog_alpha)
                        : observed;
                    have_cog = true;
                }
            }
            if (std::isnan(table[i]) && have_cog && !new_segment) {
                table[i] = blended_cog + offset;
                ++stats.from_cog;
            }
            previous = i;
            have_previous = true;
        }

        size_t begin = 0;
        while (begin < order.size()) {
            while (begin < order.size() && !positions[begin].valid) ++begin;
            if (begin == order.size()) break;
            size_t end = begin + 1;
            while (end < order.size() && sssnavcontinuity::isContinuousPair(
                    positions[end - 1], positions[end], pings[order[end - 1]],
                    pings[order[end]], continuity)) ++end;
            size_t first = begin;
            while (first < end && std::isnan(table[first])) ++first;
            if (first < end)
                for (size_t i = begin; i < first; ++i) table[i] = table[first];
            begin = end;
        }
    }

    if (want_sensor || want_ship) {
        for (size_t i = 0; i < order.size(); ++i) {
            if (!std::isnan(table[i])) continue;
            const float legacy = pings[order[i]].nav.heading_deg;
            if (legacyOk(legacy)) {
                table[i] = legacy * kDegToRad + offset;
                ++stats.from_sensor;
            }
        }
    }
    for (size_t i = 0; i < order.size(); ++i)
        if (std::isnan(table[i]) && positions[i].valid) ++stats.skipped;
    if (out_stats) *out_stats = stats;
    return table;
}

} // namespace dolphin::ui::sssheading
