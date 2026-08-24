#include "ui/features/map/sidescan/SssNavContinuity.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dolphin::ui::sssnavcontinuity {
namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr int64_t kMaxChannelPairDeltaUs = 100'000LL;

core::NavPoint asNavPoint(const CorrectedSssNav& nav)
{
    core::NavPoint point;
    point.lat = nav.lat;
    point.lon = nav.lon;
    point.valid = nav.valid;
    point.is_projected = nav.is_projected;
    point.spatial_ref = nav.spatial_ref;
    return point;
}

} // namespace

bool compatibleFrames(const CorrectedSssNav& a, const CorrectedSssNav& b)
{
    if (a.is_projected != b.is_projected) return false;
    if (!a.spatial_ref.id.empty() && !b.spatial_ref.id.empty()
            && a.spatial_ref.id != b.spatial_ref.id) return false;
    return a.spatial_ref.kind == core::SpatialRefKind::Unknown
        || b.spatial_ref.kind == core::SpatialRefKind::Unknown
        || a.spatial_ref.kind == b.spatial_ref.kind;
}

double distanceMetres(const CorrectedSssNav& a, const CorrectedSssNav& b)
{
    return geo::navDistanceMetres(asNavPoint(a), asNavPoint(b));
}

double headingBetween(const CorrectedSssNav& from, const CorrectedSssNav& to)
{
    if (from.is_projected)
        return geo::headingFromNavDeltaRad(asNavPoint(from), asNavPoint(to));
    const double mean_lat = (from.lat + to.lat) * 0.5 * kDegToRad;
    const double east = std::remainder(to.lon - from.lon, 360.0)
                      * std::max(1e-6, std::cos(mean_lat));
    return std::atan2(east, to.lat - from.lat);
}

bool samePingCycle(const core::SidescanPing& a, const core::SidescanPing& b)
{
    if (a.channel == b.channel) return false;
    if (a.ping_number != 0 && b.ping_number != 0) {
        if (a.ping_number != b.ping_number) return false;
        return a.timestamp_us <= 0 || b.timestamp_us <= 0
            || std::abs(b.timestamp_us - a.timestamp_us) <= kMaxChannelPairDeltaUs;
    }
    return a.timestamp_us > 0 && b.timestamp_us > 0
        && std::abs(b.timestamp_us - a.timestamp_us) <= kMaxChannelPairDeltaUs;
}

bool positionsCoincide(const CorrectedSssNav& a, const CorrectedSssNav& b)
{
    constexpr double kToleranceM = 0.01;
    return a.valid && b.valid && compatibleFrames(a, b)
        && distanceMetres(a, b) <= kToleranceM;
}

double interpolateLongitude(double left, double right, double alpha,
                            bool is_projected)
{
    if (is_projected) return left + (right - left) * alpha;
    return std::remainder(left + std::remainder(right - left, 360.0) * alpha,
                          360.0);
}

ssscontinuity::Thresholds deriveThresholds(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const std::vector<CorrectedSssNav>& positions)
{
    std::vector<double> nav, time, ping;
    for (size_t i = 1; i < positions.size(); ++i) {
        const auto& a = positions[i - 1];
        const auto& b = positions[i];
        if (!a.valid || !b.valid || !compatibleFrames(a, b)
                || positionsCoincide(a, b)) continue;
        nav.push_back(distanceMetres(a, b));
        const auto& pa = pings[order[i - 1]];
        const auto& pb = pings[order[i]];
        if (pa.timestamp_us > 0 && pb.timestamp_us > pa.timestamp_us)
            time.push_back(static_cast<double>(pb.timestamp_us - pa.timestamp_us));
        if (pa.ping_number > 0 && pb.ping_number > pa.ping_number)
            ping.push_back(static_cast<double>(pb.ping_number - pa.ping_number));
    }
    return ssscontinuity::fromDeltas(
        std::move(nav), std::move(time), std::move(ping));
}

bool isContinuousPair(const CorrectedSssNav& a,
                      const CorrectedSssNav& b,
                      const core::SidescanPing& a_ping,
                      const core::SidescanPing& b_ping,
                      const ssscontinuity::Thresholds& thresholds)
{
    if (!a.valid || !b.valid || !compatibleFrames(a, b)) return false;
    if (a_ping.timestamp_us > 0 && b_ping.timestamp_us > 0) {
        const int64_t delta = b_ping.timestamp_us - a_ping.timestamp_us;
        if (delta < 0 || delta > thresholds.time_gap_us) return false;
    }
    if (a_ping.ping_number > 0 && b_ping.ping_number > 0
            && !samePingCycle(a_ping, b_ping)) {
        if (b_ping.ping_number <= a_ping.ping_number
                || b_ping.ping_number - a_ping.ping_number > thresholds.ping_gap)
            return false;
    }
    const double step = distanceMetres(a, b);
    return std::isfinite(step) && step <= thresholds.nav_gap_m;
}

} // namespace dolphin::ui::sssnavcontinuity
