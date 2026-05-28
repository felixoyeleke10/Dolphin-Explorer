#include "geo/NavSmoother.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>

namespace dolphin::geo {

static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double RAD_TO_DEG = 180.0 / M_PI;
static constexpr double EARTH_R    = 6371000.0; // metres


NavSmoother::NavSmoother(const NavSmootherParams& params)
    : m_params(params)
{}

std::vector<core::NavPoint> NavSmoother::smooth(
    const std::vector<core::NavPoint>& raw) const
{
    auto despiked = rejectSpikes(raw);
    auto filtered = kalmanFilter(despiked);
    if (m_params.layback_m != 0.0f)
        applyLayback(filtered);
    return filtered;
}

std::vector<core::NavPoint> NavSmoother::rejectSpikes(
    const std::vector<core::NavPoint>& in) const
{
    if (in.size() < 2) return in;

    std::vector<core::NavPoint> out;
    out.reserve(in.size());
    out.push_back(in[0]);

    for (size_t i = 1; i < in.size(); ++i) {
        double dt    = in[i].timestamp - in[i - 1].timestamp;
        double dist  = geo::navDistanceMetres(in[i - 1], in[i]);
        double speed = (dt > 0.0) ? dist / dt : 0.0; // m/s
        if (speed <= m_params.spike_max_speed_ms)
            out.push_back(in[i]);
    }

    return out;
}

std::vector<core::NavPoint> NavSmoother::kalmanFilter(
    const std::vector<core::NavPoint>& in) const
{
    if (in.empty()) return {};

    std::vector<core::NavPoint> out = in;
    const bool projected = geo::navUsesProjectedCoordinates(in.front());
    const double anchor_lat = in.front().lat;
    const double anchor_lon = in.front().lon;
    const double cos_anchor = std::max(1e-6, std::cos(anchor_lat * DEG_TO_RAD));

    auto toFilterSpace = [&](const core::NavPoint& nav, double& y_m, double& x_m) {
        if (projected) {
            y_m = nav.lat;
            x_m = nav.lon;
            return;
        }
        y_m = (nav.lat - anchor_lat) * DEG_TO_RAD * EARTH_R;
        x_m = (nav.lon - anchor_lon) * DEG_TO_RAD * EARTH_R * cos_anchor;
    };

    auto fromFilterSpace = [&](double y_m, double x_m, core::NavPoint& nav) {
        if (projected) {
            nav.lat = y_m;
            nav.lon = x_m;
            return;
        }
        nav.lat = anchor_lat + (y_m / EARTH_R) * RAD_TO_DEG;
        nav.lon = anchor_lon + (x_m / (EARTH_R * cos_anchor)) * RAD_TO_DEG;
    };

    double Q  = m_params.process_noise;
    double R  = m_params.measure_noise;
    double px = 1.0;
    double py = 1.0;
    double yhat = 0.0;
    double xhat = 0.0;
    toFilterSpace(in.front(), yhat, xhat);

    for (size_t i = 0; i < in.size(); ++i) {
        double y_meas = 0.0;
        double x_meas = 0.0;
        toFilterSpace(in[i], y_meas, x_meas);

        // Scale process noise by dt^2 so widely spaced fixes widen uncertainty.
        double dt = (i > 0) ? (in[i].timestamp - in[i - 1].timestamp) : 1.0;
        if (dt <= 0.0) dt = 1.0;
        px += Q * dt * dt;
        py += Q * dt * dt;

        double Kx = px / (px + R);
        double Ky = py / (py + R);
        xhat = xhat + Kx * (x_meas - xhat);
        yhat = yhat + Ky * (y_meas - yhat);
        px *= (1.0 - Kx);
        py *= (1.0 - Ky);

        fromFilterSpace(yhat, xhat, out[i]);
    }

    return out;
}

void NavSmoother::applyLayback(std::vector<core::NavPoint>& nav) const
{
    if (nav.size() < 2) return;

    const float lb = m_params.layback_m;

    for (auto& point : nav) {
        const double heading_rad = point.heading_deg * DEG_TO_RAD;
        const double east_m = -static_cast<double>(lb) * std::sin(heading_rad);
        const double north_m = -static_cast<double>(lb) * std::cos(heading_rad);
        double out_lon = 0.0;
        double out_lat = 0.0;
        if (geo::offsetNavByGroundMetres(point, east_m, north_m, out_lon, out_lat)) {
            point.lon = out_lon;
            point.lat = out_lat;
        }
    }
}

} // namespace dolphin::geo
