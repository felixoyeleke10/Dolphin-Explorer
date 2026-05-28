#pragma once
#include <vector>
#include "core/NavPoint.h"

namespace dolphin::geo {

struct NavSmootherParams {
    float process_noise  = 0.01f;  // Kalman Q — trust model vs measurement
    float measure_noise  = 1.0f;   // Kalman R — GPS noise (meters)
    float spike_max_speed_ms = 5.0f;  // reject fixes whose speed exceeds this (m/s)
    float layback_m      = 0.0f;   // tow cable layback distance
    float layback_offset = 0.0f;   // cross-track offset
};

class NavSmoother {
public:
    explicit NavSmoother(const NavSmootherParams& params = {});

    // Returns smoothed nav track. Input must be sorted by timestamp.
    std::vector<core::NavPoint> smooth(const std::vector<core::NavPoint>& raw) const;

private:
    NavSmootherParams m_params;

    std::vector<core::NavPoint> rejectSpikes(const std::vector<core::NavPoint>& in) const;
    std::vector<core::NavPoint> kalmanFilter(const std::vector<core::NavPoint>& in) const;
    void applyLayback(std::vector<core::NavPoint>& nav) const;
};

} // namespace dolphin::geo
