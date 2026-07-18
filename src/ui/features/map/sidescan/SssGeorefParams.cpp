// SssGeorefParams.cpp — shared heading/position resolvers for sidescan georeferencing.
//
// Used by both SidescanSwathGeoreferencer (mosaic/preview) and
// SssMapBuild::buildSwathCoverage (ribbon footprints) so that coverage and
// mosaic always share identical per-ping heading and position.

#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "ui/features/map/sidescan/SssContinuity.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace dolphin::ui {

namespace {

constexpr double kDegToRad     = std::numbers::pi / 180.0;
constexpr double kHeadingBlend = 0.2;   // EMA alpha for smoothed COG
constexpr int64_t kMaxChannelPairDeltaUs = 100'000LL; // one firing cycle

// Automatic nav repair accepts low-rate GPS only while the intervening sonar
// cycles prove the line remained continuous and the anchor motion is physically
// plausible. These hard ceilings are deliberately much larger than normal
// tow-survey motion but far below a line/project break.
constexpr int64_t kRepairHardSpanUs       = 60'000'000LL;
constexpr double  kRepairHardDistanceM    = 1'000.0;
constexpr double  kRepairMaxSpeedMps      = 15.0;
constexpr double  kRepairCadenceFactor    = 4.0;
constexpr int64_t kRepairTimeGapFloorUs   = 5'000'000LL;
constexpr int64_t kRepairTimeGapCeilingUs = 30'000'000LL;
constexpr uint32_t kRepairPingGapFloor    = 20;
constexpr uint32_t kRepairPingGapCeiling  = 1'000;
constexpr size_t kRepairMinCadenceSamples = 3;
constexpr double kCyclePoseMergeToleranceM = 2.0;

struct NavRepairPolicy {
    int64_t  max_cycle_gap_us = kRepairTimeGapFloorUs;
    uint32_t max_ping_advance = kRepairPingGapFloor;
};

bool samePingCycle(const core::SidescanPing& a, const core::SidescanPing& b);

bool isUsableNavPoint(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) && (lat != 0.0 || lon != 0.0);
}

bool isValidHeadingField(float h)
{
    return std::isfinite(h) && h != 0.0f;
}

// Returns true if at least one finite non-zero value exists in a float field
// across the ordered ping set.  Used to decide whether 0.0 can be a real value.
template <typename FieldFn>
bool fieldHasNonZero(const std::vector<core::SidescanPing>& pings,
                     const std::vector<size_t>& order,
                     FieldFn fn)
{
    for (size_t i = 0; i < order.size(); ++i) {
        const float v = fn(pings[order[i]].nav);
        if (std::isfinite(v) && v != 0.0f) return true;
    }
    return false;
}

core::NavPoint asNavPoint(const CorrectedSssNav& nav)
{
    core::NavPoint point;
    point.lat          = nav.lat;
    point.lon          = nav.lon;
    point.valid        = nav.valid;
    point.is_projected = nav.is_projected;
    point.spatial_ref  = nav.spatial_ref;
    return point;
}

bool haveCompatibleCoordinateFrames(const CorrectedSssNav& a,
                                    const CorrectedSssNav& b)
{
    if (a.is_projected != b.is_projected)
        return false;

    if (!a.spatial_ref.id.empty() && !b.spatial_ref.id.empty()
        && a.spatial_ref.id != b.spatial_ref.id)
        return false;

    if (a.spatial_ref.kind != core::SpatialRefKind::Unknown
        && b.spatial_ref.kind != core::SpatialRefKind::Unknown
        && a.spatial_ref.kind != b.spatial_ref.kind)
        return false;

    return true;
}

double navStepMetres(const CorrectedSssNav& a, const CorrectedSssNav& b)
{
    return geo::navDistanceMetres(asNavPoint(a), asNavPoint(b));
}

double headingBetween(const CorrectedSssNav& from, const CorrectedSssNav& to)
{
    if (from.is_projected)
        return geo::headingFromNavDeltaRad(asNavPoint(from), asNavPoint(to));

    const double mean_lat_rad = (from.lat + to.lat) * 0.5 * kDegToRad;
    const double east_delta = std::remainder(to.lon - from.lon, 360.0)
                            * std::max(1e-6, std::cos(mean_lat_rad));
    const double north_delta = to.lat - from.lat;
    return std::atan2(east_delta, north_delta);
}

bool repairMetadataContinues(
    const std::vector<int64_t>&            timestamps,
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const NavRepairPolicy&                 policy,
    size_t                                 previous_index,
    size_t                                 current_index)
{
    const auto& previous_ping = pings[order[previous_index]];
    const auto& current_ping  = pings[order[current_index]];
    const bool same_cycle = samePingCycle(previous_ping, current_ping);
    const int64_t dt = timestamps[current_index] - timestamps[previous_index];

    if (same_cycle)
        return dt == 0;
    if (dt <= 0 || dt > policy.max_cycle_gap_us)
        return false;

    if (previous_ping.ping_number > 0 && current_ping.ping_number > 0) {
        if (current_ping.ping_number <= previous_ping.ping_number)
            return false;
        if (current_ping.ping_number - previous_ping.ping_number
            > policy.max_ping_advance)
            return false;
    }
    return true;
}

bool isBoundedInterpolationInterval(
    const CorrectedSssNav&                 left,
    const CorrectedSssNav&                 right,
    int64_t                                left_ts,
    int64_t                                right_ts,
    const std::vector<int64_t>&            timestamps,
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const NavRepairPolicy&                 policy,
    size_t                                 left_index,
    size_t                                 right_index)
{
    if (!left.valid || !right.valid || !haveCompatibleCoordinateFrames(left, right))
        return false;

    // A positive, strictly increasing timestamp span is required. Without it,
    // index-based interpolation would invent vessel motion and speed.
    if (left_ts <= 0 || right_ts <= left_ts
        || right_ts - left_ts > kRepairHardSpanUs)
        return false;

    const double step_m = navStepMetres(left, right);
    const double span_s = static_cast<double>(right_ts - left_ts) / 1'000'000.0;
    if (!std::isfinite(step_m) || step_m > kRepairHardDistanceM
        || step_m / span_s > kRepairMaxSpeedMps)
        return false;

    // Every intervening firing cycle must follow the robust local cadence. A
    // ping-number reset/reuse is a hard segment boundary, except for the paired
    // port/starboard records that samePingCycle() identifies as one firing.
    for (size_t i = left_index + 1; i <= right_index; ++i) {
        if (!repairMetadataContinues(
                timestamps, pings, order, policy, i - 1, i)
            || timestamps[i] < left_ts || timestamps[i] > right_ts)
            return false;
    }
    return true;
}

double interpolateLongitude(double left_lon,
                            double right_lon,
                            double alpha,
                            bool is_projected)
{
    if (is_projected)
        return left_lon + (right_lon - left_lon) * alpha;

    // Follow the short arc across the antimeridian instead of interpolating
    // through longitude zero.
    const double delta = std::remainder(right_lon - left_lon, 360.0);
    return std::remainder(left_lon + delta * alpha, 360.0);
}

void interpolateNavInterior(
    std::vector<CorrectedSssNav>&          table,
    const std::vector<int64_t>&            timestamps,
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const NavRepairPolicy&                 policy,
    size_t                                 left_index,
    size_t                                 right_index)
{
    const CorrectedSssNav left  = table[left_index];
    const CorrectedSssNav right = table[right_index];
    const int64_t left_ts  = timestamps[left_index];
    const int64_t right_ts = timestamps[right_index];

    if (!isBoundedInterpolationInterval(left, right, left_ts, right_ts,
            timestamps, pings, order, policy, left_index, right_index))
        return;

    const double span = static_cast<double>(right_ts - left_ts);
    const uint32_t common_flags = left.flags & right.flags;
    const core::SpatialRef repaired_ref = !left.spatial_ref.empty()
        ? left.spatial_ref
        : right.spatial_ref;

    for (size_t i = left_index + 1; i < right_index; ++i) {
        const int64_t ts = timestamps[i];
        const double alpha = static_cast<double>(ts - left_ts) / span;
        table[i].lat = left.lat + (right.lat - left.lat) * alpha;
        table[i].lon = interpolateLongitude(left.lon, right.lon, alpha,
                                             left.is_projected);
        table[i].valid        = true;
        table[i].is_projected = left.is_projected;
        table[i].spatial_ref  = repaired_ref;
        table[i].flags       |= common_flags | kNavFlagInterpolated;
    }
}

bool positionsCoincide(const CorrectedSssNav& a, const CorrectedSssNav& b)
{
    constexpr double kRepeatedFixToleranceM = 0.01; // one centimetre
    return a.valid && b.valid
        && haveCompatibleCoordinateFrames(a, b)
        && navStepMetres(a, b) <= kRepeatedFixToleranceM;
}

bool samePingCycle(const core::SidescanPing& a, const core::SidescanPing& b)
{
    if (a.channel == b.channel)
        return false;

    if (a.ping_number != 0 && b.ping_number != 0) {
        if (a.ping_number != b.ping_number)
            return false;
        if (a.timestamp_us > 0 && b.timestamp_us > 0
            && std::abs(b.timestamp_us - a.timestamp_us) > kMaxChannelPairDeltaUs)
            return false;
        return true;
    }
    return a.timestamp_us > 0 && b.timestamp_us > 0
        && std::abs(b.timestamp_us - a.timestamp_us) <= kMaxChannelPairDeltaUs;
}

std::vector<int64_t> buildCycleTimestamps(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order)
{
    std::vector<int64_t> timestamps(order.size());
    size_t begin = 0;
    while (begin < order.size()) {
        size_t end = begin + 1;
        while (end < order.size()
               && samePingCycle(pings[order[begin]], pings[order[end]]))
            ++end;

        int64_t first_ts = 0;
        int64_t last_ts  = 0;
        for (size_t i = begin; i < end; ++i) {
            const int64_t ts = pings[order[i]].timestamp_us;
            if (ts <= 0)
                continue;
            if (first_ts == 0 || ts < first_ts)
                first_ts = ts;
            if (last_ts == 0 || ts > last_ts)
                last_ts = ts;
        }

        const int64_t cycle_ts = first_ts > 0
            ? first_ts + (last_ts - first_ts) / 2
            : 0;
        for (size_t i = begin; i < end; ++i)
            timestamps[i] = cycle_ts;
        begin = end;
    }
    return timestamps;
}

double repairCadenceLimit(std::vector<double> deltas,
                          double floor,
                          double ceiling)
{
    deltas.erase(std::remove_if(deltas.begin(), deltas.end(),
        [](double value) { return !std::isfinite(value) || value <= 0.0; }),
        deltas.end());
    if (deltas.size() < kRepairMinCadenceSamples)
        return floor;

    const size_t mid = deltas.size() / 2;
    std::nth_element(deltas.begin(), deltas.begin() + mid, deltas.end());
    return std::clamp(deltas[mid] * kRepairCadenceFactor, floor, ceiling);
}

NavRepairPolicy deriveNavRepairPolicy(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const std::vector<int64_t>&            timestamps)
{
    std::vector<double> cycle_deltas;
    std::vector<double> ping_advances;
    if (order.size() > 1) {
        cycle_deltas.reserve(order.size() - 1);
        ping_advances.reserve(order.size() - 1);
    }

    for (size_t i = 1; i < order.size(); ++i) {
        const auto& previous = pings[order[i - 1]];
        const auto& current  = pings[order[i]];
        if (samePingCycle(previous, current))
            continue;

        const int64_t dt = timestamps[i] - timestamps[i - 1];
        if (dt > 0)
            cycle_deltas.push_back(static_cast<double>(dt));

        if (previous.ping_number > 0
            && current.ping_number > previous.ping_number)
            ping_advances.push_back(static_cast<double>(
                current.ping_number - previous.ping_number));
    }

    NavRepairPolicy policy;
    policy.max_cycle_gap_us = static_cast<int64_t>(std::llround(
        repairCadenceLimit(std::move(cycle_deltas),
            static_cast<double>(kRepairTimeGapFloorUs),
            static_cast<double>(kRepairTimeGapCeilingUs))));
    policy.max_ping_advance = static_cast<uint32_t>(std::llround(
        repairCadenceLimit(std::move(ping_advances),
            static_cast<double>(kRepairPingGapFloor),
            static_cast<double>(kRepairPingGapCeiling))));
    return policy;
}

void shareValidPositionWithinCycles(
    std::vector<CorrectedSssNav>&          table,
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order)
{
    size_t begin = 0;
    while (begin < order.size()) {
        size_t end = begin + 1;
        while (end < order.size()
               && samePingCycle(pings[order[begin]], pings[order[end]]))
            ++end;

        size_t anchor = end;
        for (size_t i = begin; i < end; ++i) {
            if (table[i].valid) {
                anchor = i;
                break;
            }
        }

        if (anchor < end) {
            CorrectedSssNav cycle_pose = table[anchor];
            size_t valid_count = 1;
            bool merge_cycle = true;
            for (size_t i = anchor + 1; i < end; ++i) {
                if (!table[i].valid)
                    continue;
                if (!haveCompatibleCoordinateFrames(cycle_pose, table[i])
                    || navStepMetres(cycle_pose, table[i])
                       > kCyclePoseMergeToleranceM) {
                    merge_cycle = false;
                    break;
                }

                // A firing cycle represents one sensor pose. Average the tiny
                // channel-header discrepancy instead of privileging either
                // channel, while retaining a hard refusal for gross conflicts.
                const double alpha = 1.0 / static_cast<double>(valid_count + 1);
                cycle_pose.lat += (table[i].lat - cycle_pose.lat) * alpha;
                cycle_pose.lon = interpolateLongitude(
                    cycle_pose.lon, table[i].lon, alpha,
                    cycle_pose.is_projected);
                cycle_pose.flags |= table[i].flags;
                ++valid_count;
            }

            if (!merge_cycle) {
                begin = end;
                continue;
            }

            for (size_t i = begin; i < end; ++i) {
                // Copy only the measured position. Final-table entries may
                // already carry an independent instrument heading that must
                // not be overwritten by the companion channel's heading.
                const bool changed = !table[i].valid
                    || table[i].lat != cycle_pose.lat
                    || table[i].lon != cycle_pose.lon;
                table[i].lat          = cycle_pose.lat;
                table[i].lon          = cycle_pose.lon;
                table[i].valid        = true;
                table[i].is_projected = cycle_pose.is_projected;
                table[i].spatial_ref  = cycle_pose.spatial_ref;
                table[i].flags       |= cycle_pose.flags;
                if (changed)
                    table[i].flags |= kNavFlagInterpolated;
            }
        }
        begin = end;
    }
}

// Repairs two distinct cases, both only between trustworthy anchors:
//   1. invalid interior nav runs;
//   2. repeated GPS fixes followed by a new fix.
// Leading/trailing runs and intervals that cross a time/spatial break are left
// untouched. Timestamp interpolation keeps port/starboard pings captured at the
// same instant at the same centre position.
void repairBoundedNavRuns(
    std::vector<CorrectedSssNav>&          table,
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order)
{
    const size_t n = table.size();
    if (n < 2)
        return;

    const std::vector<int64_t> timestamps = buildCycleTimestamps(pings, order);
    const NavRepairPolicy policy = deriveNavRepairPolicy(pings, order, timestamps);

    // Port and starboard records from one firing cycle describe one sensor
    // pose. If one channel omitted nav, inherit the other channel's measured
    // pose before considering any across-cycle interpolation.
    shareValidPositionWithinCycles(table, pings, order);

    // Pass 1: fill contiguous missing runs bracketed by immediate valid fixes.
    size_t i = 0;
    while (i < n) {
        if (table[i].valid) {
            ++i;
            continue;
        }

        const size_t run_begin = i;
        while (i < n && !table[i].valid)
            ++i;

        if (run_begin > 0 && i < n)
            interpolateNavInterior(
                table, timestamps, pings, order, policy, run_begin - 1, i);
    }

    // Pass 2: spread a held GPS fix over ping timestamps up to the next
    // distinct fix. Unbounded holds at the end of a line remain untouched.
    size_t left = 0;
    while (left < n) {
        if (!table[left].valid) {
            ++left;
            continue;
        }

        size_t right = left + 1;
        while (right < n && positionsCoincide(table[left], table[right])
               && repairMetadataContinues(
                   timestamps, pings, order, policy, right - 1, right))
            ++right;

        if (right > left + 1 && right < n && table[right].valid)
            interpolateNavInterior(
                table, timestamps, pings, order, policy, left, right);

        left = (right > left + 1) ? right : left + 1;
    }
}

std::vector<CorrectedSssNav> buildBasePositionTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params)
{
    std::vector<CorrectedSssNav> table(order.size());
    const double no_heading = std::numeric_limits<double>::quiet_NaN();

    for (size_t i = 0; i < order.size(); ++i) {
        const auto pos = resolveSssPosition(pings[order[i]], params, no_heading);
        table[i].lat          = pos.lat;
        table[i].lon          = pos.lon;
        table[i].valid        = pos.valid;
        table[i].is_projected = pos.is_projected;
        table[i].spatial_ref  = pos.spatial_ref;
        table[i].flags        = pos.flags;
    }

    repairBoundedNavRuns(table, pings, order);
    return table;
}

ssscontinuity::Thresholds retainedContinuityThresholds(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const std::vector<CorrectedSssNav>&    positions)
{
    std::vector<double> nav_deltas;
    std::vector<double> time_deltas;
    std::vector<double> ping_deltas;
    if (positions.size() > 1) {
        nav_deltas.reserve(positions.size() - 1);
        time_deltas.reserve(positions.size() - 1);
        ping_deltas.reserve(positions.size() - 1);
    }

    for (size_t i = 1; i < positions.size(); ++i) {
        const auto& a = positions[i - 1];
        const auto& b = positions[i];
        if (!a.valid || !b.valid || !haveCompatibleCoordinateFrames(a, b)
                || positionsCoincide(a, b))
            continue;

        nav_deltas.push_back(navStepMetres(a, b));
        const auto& pa = pings[order[i - 1]];
        const auto& pb = pings[order[i]];
        if (pa.timestamp_us > 0 && pb.timestamp_us > pa.timestamp_us)
            time_deltas.push_back(static_cast<double>(
                pb.timestamp_us - pa.timestamp_us));
        if (pa.ping_number > 0 && pb.ping_number > pa.ping_number)
            ping_deltas.push_back(static_cast<double>(
                pb.ping_number - pa.ping_number));
    }
    return ssscontinuity::fromDeltas(
        std::move(nav_deltas), std::move(time_deltas), std::move(ping_deltas));
}

bool isContinuousNavPair(const CorrectedSssNav&       a,
                         const CorrectedSssNav&       b,
                         const core::SidescanPing&    a_ping,
                         const core::SidescanPing&    b_ping,
                         const ssscontinuity::Thresholds& thresholds)
{
    if (!a.valid || !b.valid || !haveCompatibleCoordinateFrames(a, b))
        return false;

    if (a_ping.timestamp_us > 0 && b_ping.timestamp_us > 0) {
        const int64_t dt = b_ping.timestamp_us - a_ping.timestamp_us;
        if (dt < 0 || dt > thresholds.time_gap_us)
            return false;
    }

    if (a_ping.ping_number > 0 && b_ping.ping_number > 0
        && !samePingCycle(a_ping, b_ping)) {
        // Equal numbers outside a tightly matched dual-channel firing are a
        // reuse; a lower number is a reset/wrap. Both start a new segment.
        if (b_ping.ping_number <= a_ping.ping_number
            || b_ping.ping_number - a_ping.ping_number > thresholds.ping_gap)
            return false;
    }

    const double step_m = navStepMetres(a, b);
    return std::isfinite(step_m) && step_m <= thresholds.nav_gap_m;
}

std::vector<double> buildHeadingTableFromPositions(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    const std::vector<CorrectedSssNav>&    positions,
    HeadingStats*                          out_stats)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    const size_t n = order.size();
    std::vector<double> table(n, kNaN);
    HeadingStats stats;
    if (positions.size() != n) {
        if (out_stats) *out_stats = stats;
        return table;
    }

    const double offset_rad = params.heading_offset_deg * kDegToRad;
    const ssscontinuity::Thresholds continuity =
        retainedContinuityThresholds(pings, order, positions);
    const bool want_sensor = params.heading_source == SssHeadingSource::Auto
                          || params.heading_source == SssHeadingSource::FishSensor;
    const bool want_ship   = params.heading_source == SssHeadingSource::Auto
                          || params.heading_source == SssHeadingSource::VesselShip;
    const bool want_cog    = params.heading_source == SssHeadingSource::Auto
                          || params.heading_source == SssHeadingSource::CourseOverGround
                          || params.heading_source == SssHeadingSource::SmoothedCourseOverGround;
    const double cog_alpha = params.heading_source == SssHeadingSource::CourseOverGround
        ? 1.0
        : kHeadingBlend;

    const bool legacy_has_data = fieldHasNonZero(pings, order,
        [](const core::NavPoint& nav) { return nav.heading_deg; });
    auto legacyOk = [legacy_has_data](float heading) {
        return std::isfinite(heading) && (legacy_has_data || heading != 0.0f);
    };

    // Explicit instrument headings are independent of position validity. This
    // matters when a channel has a usable gyro sample but its GPS fix is repaired
    // from the companion channel or adjacent bounded fixes.
    if (want_sensor || want_ship) {
        const bool sensor_has_data = fieldHasNonZero(pings, order,
            [](const core::NavPoint& nav) { return nav.sensor_heading_deg; });
        const bool ship_has_data = fieldHasNonZero(pings, order,
            [](const core::NavPoint& nav) { return nav.ship_heading_deg; });
        auto sensorOk = [sensor_has_data](float heading) {
            return std::isfinite(heading) && (sensor_has_data || heading != 0.0f);
        };
        auto shipOk = [ship_has_data](float heading) {
            return std::isfinite(heading) && (ship_has_data || heading != 0.0f);
        };

        for (size_t i = 0; i < n; ++i) {
            const auto& nav = pings[order[i]].nav;
            if (want_sensor && sensorOk(nav.sensor_heading_deg)) {
                table[i] = static_cast<double>(nav.sensor_heading_deg) * kDegToRad
                         + offset_rad;
                ++stats.from_sensor;
            } else if (want_ship && shipOk(nav.ship_heading_deg)) {
                table[i] = static_cast<double>(nav.ship_heading_deg) * kDegToRad
                         + offset_rad;
                ++stats.from_ship;
            }
        }
    }

    if (want_cog) {
        bool   have_previous = false;
        size_t previous      = 0;
        bool   have_cog      = false;
        double cog_blend     = 0.0;

        for (size_t i = 0; i < n; ++i) {
            if (!positions[i].valid) {
                have_previous = false;
                have_cog      = false;
                continue;
            }

            bool starts_new_segment = false;
            if (have_previous) {
                if (!isContinuousNavPair(positions[previous], positions[i],
                                         pings[order[previous]], pings[order[i]],
                                         continuity)) {
                    have_cog = false;
                    starts_new_segment = true;
                } else if (!positionsCoincide(positions[previous], positions[i])) {
                    const double observed = headingBetween(
                        positions[previous], positions[i]);
                    cog_blend = have_cog
                        ? geo::blendAngleRad(cog_blend, observed, cog_alpha)
                        : observed;
                    have_cog = true;
                }
            }

            if (std::isnan(table[i]) && have_cog && !starts_new_segment) {
                table[i] = cog_blend + offset_rad;
                ++stats.from_cog;
            }

            previous      = i;
            have_previous = true;
        }

        // Backfill only the startup portion of each continuous survey segment.
        // A global backfill would manufacture headings across missing nav,
        // >5-second line breaks, or >50-metre jumps.
        size_t segment_begin = 0;
        while (segment_begin < n) {
            while (segment_begin < n && !positions[segment_begin].valid)
                ++segment_begin;
            if (segment_begin == n)
                break;

            size_t segment_end = segment_begin + 1;
            while (segment_end < n
                   && isContinuousNavPair(
                       positions[segment_end - 1], positions[segment_end],
                       pings[order[segment_end - 1]],
                       pings[order[segment_end]], continuity))
                ++segment_end;

            size_t first_heading = segment_begin;
            while (first_heading < segment_end && std::isnan(table[first_heading]))
                ++first_heading;
            if (first_heading < segment_end) {
                for (size_t i = segment_begin; i < first_heading; ++i)
                    table[i] = table[first_heading];
            }
            segment_begin = segment_end;
        }
    }

    // Legacy cache fallback after selected instrument fields and repaired-track
    // COG, preserving the established priority order.
    if (want_sensor || want_ship) {
        for (size_t i = 0; i < n; ++i) {
            if (!std::isnan(table[i]))
                continue;
            const float legacy_heading = pings[order[i]].nav.heading_deg;
            if (legacyOk(legacy_heading)) {
                table[i] = static_cast<double>(legacy_heading) * kDegToRad
                         + offset_rad;
                ++stats.from_sensor;
            }
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(table[i]) && positions[i].valid)
            ++stats.skipped;
    }

    if (out_stats) *out_stats = stats;
    return table;
}

// Apply moving-average (or spike rejection) smoothing to CorrectedSssNav positions.
// Never smooths across the retained-track continuity boundaries.
void applyNavSmoothing(std::vector<CorrectedSssNav>&         table,
                       const std::vector<core::SidescanPing>& pings,
                       const std::vector<size_t>&              order,
                       const SssGeorefParams&                  params)
{
    const size_t n = table.size();
    if (n < 2) return;

    const ssscontinuity::Thresholds continuity =
        retainedContinuityThresholds(pings, order, table);

    if (params.smoothing_mode == SssNavSmoothingMode::SpikeRejection) {
        // Correct only an isolated excursion that returns to the local track.
        // The former hold-last-good policy flattened every legitimate retained
        // step above 50 m and could freeze an entire line after a real line break.
        const std::vector<CorrectedSssNav> original = table;
        for (const auto channel : {core::SidescanChannel::Port,
                                   core::SidescanChannel::Starboard}) {
            std::vector<size_t> channel_indices;
            for (size_t i = 0; i < n; ++i)
                if (pings[order[i]].channel == channel && original[i].valid)
                    channel_indices.push_back(i);

            for (size_t j = 1; j + 1 < channel_indices.size(); ++j) {
                const size_t previous = channel_indices[j - 1];
                const size_t current  = channel_indices[j];
                const size_t next     = channel_indices[j + 1];
                if (!isContinuousNavPair(original[previous], original[next],
                        pings[order[previous]], pings[order[next]], continuity))
                    continue;
                if (navStepMetres(original[previous], original[current])
                        <= continuity.nav_gap_m
                    || navStepMetres(original[current], original[next])
                        <= continuity.nav_gap_m)
                    continue;

                const int64_t left_ts  = pings[order[previous]].timestamp_us;
                const int64_t here_ts  = pings[order[current]].timestamp_us;
                const int64_t right_ts = pings[order[next]].timestamp_us;
                const double alpha = left_ts > 0 && here_ts >= left_ts
                        && right_ts > here_ts
                    ? static_cast<double>(here_ts - left_ts)
                      / static_cast<double>(right_ts - left_ts)
                    : 0.5;
                table[current].lat = original[previous].lat
                    + (original[next].lat - original[previous].lat) * alpha;
                table[current].lon = interpolateLongitude(
                    original[previous].lon, original[next].lon, alpha,
                    original[previous].is_projected);
                table[current].flags |= kNavFlagInterpolated;
            }
        }
        return;
    }

    if (params.smoothing_mode == SssNavSmoothingMode::MovingAverage ||
        params.smoothing_mode == SssNavSmoothingMode::Median) {
        const int hw = std::max(1, params.smoothing_window / 2);
        std::vector<CorrectedSssNav> smoothed = table;

        const auto sameSegment = [&](size_t a, size_t b) {
            const size_t first = std::min(a, b);
            const size_t last  = std::max(a, b);
            for (size_t k = first + 1; k <= last; ++k) {
                if (!isContinuousNavPair(table[k - 1], table[k],
                        pings[order[k - 1]], pings[order[k]], continuity))
                    return false;
            }
            return true;
        };

        const auto median = [](std::vector<double> values) {
            const size_t mid = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + mid, values.end());
            if ((values.size() & 1u) != 0)
                return values[mid];
            const double upper = values[mid];
            const double lower = *std::max_element(values.begin(), values.begin() + mid);
            return (lower + upper) * 0.5;
        };

        for (size_t i = 0; i < n; ++i) {
            if (!table[i].valid) continue;

            std::vector<double> latitudes;
            std::vector<double> longitudes;
            latitudes.reserve(static_cast<size_t>(hw * 2 + 1));
            longitudes.reserve(static_cast<size_t>(hw * 2 + 1));

            for (int j = static_cast<int>(i) - hw;
                 j <= static_cast<int>(i) + hw; ++j) {
                if (j < 0 || static_cast<size_t>(j) >= n) continue;
                if (!table[j].valid) continue;
                if (!sameSegment(i, static_cast<size_t>(j))) continue;
                latitudes.push_back(table[j].lat);
                const double lon = table[i].is_projected
                    ? table[j].lon
                    : table[i].lon + std::remainder(
                        table[j].lon - table[i].lon, 360.0);
                longitudes.push_back(lon);
            }

            if (latitudes.size() > 1) {
                if (params.smoothing_mode == SssNavSmoothingMode::Median) {
                    smoothed[i].lat = median(latitudes);
                    smoothed[i].lon = median(longitudes);
                } else {
                    double sum_lat = 0.0;
                    double sum_lon = 0.0;
                    for (double value : latitudes) sum_lat += value;
                    for (double value : longitudes) sum_lon += value;
                    smoothed[i].lat = sum_lat / static_cast<double>(latitudes.size());
                    smoothed[i].lon = sum_lon / static_cast<double>(longitudes.size());
                }
                if (!table[i].is_projected)
                    smoothed[i].lon = std::remainder(smoothed[i].lon, 360.0);
            }
        }
        table = std::move(smoothed);
    }
}

} // namespace

// -- resolveSssHeading ---------------------------------------------------------

ResolvedHeading resolveSssHeading(
    const core::NavPoint&  nav,
    const SssGeorefParams& params,
    double cog_rad,
    double smoothed_cog_rad)
{
    const double offset_rad = params.heading_offset_deg * kDegToRad;
    ResolvedHeading r;

    switch (params.heading_source) {
    case SssHeadingSource::FishSensor:
        if (isValidHeadingField(nav.sensor_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.sensor_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::FishSensor;
        }
        break;

    case SssHeadingSource::VesselShip:
        if (isValidHeadingField(nav.ship_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.ship_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::VesselShip;
        }
        break;

    case SssHeadingSource::CourseOverGround:
        if (!std::isnan(cog_rad)) {
            r.heading_rad = cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::CourseOverGround;
        }
        break;

    case SssHeadingSource::SmoothedCourseOverGround:
        if (!std::isnan(smoothed_cog_rad)) {
            r.heading_rad = smoothed_cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::SmoothedCourseOverGround;
        }
        break;

    case SssHeadingSource::Auto:
    default:
        // Priority: fish sensor → vessel ship → smoothed COG → raw COG
        if (isValidHeadingField(nav.sensor_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.sensor_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::FishSensor;
        } else if (isValidHeadingField(nav.ship_heading_deg)) {
            r.heading_rad = static_cast<double>(nav.ship_heading_deg) * kDegToRad
                          + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::VesselShip;
        } else if (!std::isnan(smoothed_cog_rad)) {
            r.heading_rad = smoothed_cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::SmoothedCourseOverGround;
        } else if (!std::isnan(cog_rad)) {
            r.heading_rad = cog_rad + offset_rad;
            r.valid  = true;
            r.source = SssHeadingSource::CourseOverGround;
        }
        break;
    }

    return r;
}

// -- resolveSssPosition --------------------------------------------------------

ResolvedPosition resolveSssPosition(
    const core::SidescanPing& ping,
    const SssGeorefParams&    params,
    double heading_rad)
{
    const auto& nav = ping.nav;
    ResolvedPosition r;
    r.is_projected = nav.is_projected;
    r.spatial_ref  = nav.spatial_ref;

    switch (params.nav_source) {

    case SssNavPositionSource::FishSensor:
        if (nav.fish_nav_valid && isUsableNavPoint(nav.fish_lat, nav.fish_lon)) {
            r.lat = nav.fish_lat; r.lon = nav.fish_lon;
            r.valid = true; r.flags |= kNavFlagFishPos;
        }
        break;

    case SssNavPositionSource::VesselShip:
        if (nav.vessel_nav_valid && isUsableNavPoint(nav.vessel_lat, nav.vessel_lon)) {
            r.lat = nav.vessel_lat; r.lon = nav.vessel_lon;
            r.valid = true; r.flags |= kNavFlagVesselPos;
        }
        break;

    case SssNavPositionSource::VesselLayback: {
        if (!nav.vessel_nav_valid || !isUsableNavPoint(nav.vessel_lat, nav.vessel_lon))
            break;
        r.lat = nav.vessel_lat; r.lon = nav.vessel_lon;
        r.valid = true; r.flags |= kNavFlagVesselPos;

        if (!std::isnan(heading_rad) && params.enable_layback) {
            const double lb_m = params.use_file_layback
                ? static_cast<double>(ping.layback_m)
                : params.manual_layback_m;
            if (lb_m > 0.0) {
                // Fish trails behind vessel: offset in the backward direction.
                core::NavPoint tmp;
                tmp.lat          = r.lat;
                tmp.lon          = r.lon;
                tmp.valid        = true;
                tmp.is_projected = r.is_projected;
                tmp.spatial_ref  = r.spatial_ref;
                double new_lon, new_lat;
                if (geo::offsetNavByGroundMetres(
                        tmp,
                        -lb_m * std::sin(heading_rad),
                        -lb_m * std::cos(heading_rad),
                        new_lon, new_lat)) {
                    r.lon = new_lon; r.lat = new_lat;
                    r.flags |= kNavFlagLayback;
                }
            }
        }
        break;
    }

    case SssNavPositionSource::ManualOffset:
        // Use Auto selection as base, then fall through to offset application below.
        [[fallthrough]];

    case SssNavPositionSource::Auto:
    default:
        // Fish / sensor first; vessel / ship fallback; legacy lat/lon last.
        if (nav.fish_nav_valid && isUsableNavPoint(nav.fish_lat, nav.fish_lon)) {
            r.lat = nav.fish_lat; r.lon = nav.fish_lon;
            r.valid = true; r.flags |= kNavFlagFishPos;
        } else if (nav.vessel_nav_valid && isUsableNavPoint(nav.vessel_lat, nav.vessel_lon)) {
            r.lat = nav.vessel_lat; r.lon = nav.vessel_lon;
            r.valid = true; r.flags |= kNavFlagVesselPos;
        } else if (nav.valid && isUsableNavPoint(nav.lat, nav.lon)) {
            // Backward-compat fallback for cached pings without separate fields.
            r.lat = nav.lat; r.lon = nav.lon;
            r.valid = true;
        }
        break;
    }

    // Manual x/y offsets applied to every source when non-zero.
    if (r.valid && (params.x_offset_m != 0.0 || params.y_offset_m != 0.0)) {
        core::NavPoint tmp;
        tmp.lat          = r.lat;
        tmp.lon          = r.lon;
        tmp.valid        = true;
        tmp.is_projected = r.is_projected;
        tmp.spatial_ref  = r.spatial_ref;
        double new_lon, new_lat;
        if (geo::offsetNavByGroundMetres(tmp,
                params.x_offset_m, params.y_offset_m,
                new_lon, new_lat)) {
            r.lon = new_lon; r.lat = new_lat;
            r.flags |= kNavFlagManualOffset;
        }
    }

    return r;
}

// -- buildCorrectedNavTable ----------------------------------------------------

std::vector<CorrectedSssNav> buildCorrectedNavTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    HeadingStats*                          out_stats)
{
    const size_t n = order.size();
    std::vector<CorrectedSssNav> result(n);

    // Phase 1: resolve the selected position source without layback, then repair
    // bounded missing/held fixes. COG must be derived from this continuous track
    // rather than from raw sparse GPS records.
    const std::vector<CorrectedSssNav> base_positions =
        buildBasePositionTable(pings, order, params);
    const std::vector<double> headings = buildHeadingTableFromPositions(
        pings, order, params, base_positions, out_stats);

    // Phase 2: resolve final positions with the now-known heading so vessel
    // layback can be applied, then repair the final position table using the
    // same conservative bounds.
    for (size_t i = 0; i < n; ++i) {
        result[i].heading_rad   = headings[i];
        result[i].heading_valid = !std::isnan(headings[i]);

        const auto pos = resolveSssPosition(pings[order[i]], params, headings[i]);
        result[i].lat          = pos.lat;
        result[i].lon          = pos.lon;
        result[i].valid        = pos.valid;
        result[i].is_projected = pos.is_projected;
        result[i].spatial_ref  = pos.spatial_ref;
        result[i].flags        = pos.flags;
    }
    repairBoundedNavRuns(result, pings, order);

    // Phase 3: optional nav smoothing.
    if (params.smoothing_mode != SssNavSmoothingMode::Off)
        applyNavSmoothing(result, pings, order, params);

    // Keep geographic world-space geometry on one continuous longitude branch.
    // Distance/heading calculations already use the short dateline arc, but the
    // map bbox and rasterizer are linear; canonical +180/-180 values would create
    // a nearly 360-degree mosaic. Invalid gaps do not reset the branch because a
    // resumed line still needs to remain locally continuous.
    bool have_previous_frame = false;
    bool previous_projected = false;
    double previous_lon = 0.0;
    for (auto& nav : result) {
        if (!nav.valid) continue;
        if (have_previous_frame && nav.is_projected == previous_projected
                && !nav.is_projected) {
            nav.lon = geo::unwrapLongitudeNear(nav.lon, previous_lon);
        }
        previous_lon = nav.lon;
        previous_projected = nav.is_projected;
        have_previous_frame = true;
    }

    return result;
}

// -- buildHeadingTable ---------------------------------------------------------
// Retained for backward compatibility; calls the internal heading logic directly.

std::vector<double> buildHeadingTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    HeadingStats*                          out_stats)
{
    const std::vector<CorrectedSssNav> positions =
        buildBasePositionTable(pings, order, params);
    return buildHeadingTableFromPositions(
        pings, order, params, positions, out_stats);
}

} // namespace dolphin::ui
