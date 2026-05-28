#pragma once
#include "render/sonar/SonarDisplayParams.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dolphin::ui {

// SSSAmplitudeProcessor - 16-bit sonar amplitude to display intensity.
//
// Rule of thumb for the sonar path:
//   * uint16_t preserves sample amplitude
//   * float processes and displays amplitude
//   * double positions data in the real world
struct SSSAmplitudeProcessor {
    static uint16_t physical16(uint16_t raw, float range_m) noexcept
    {
        if (range_m < 0.f) return 0;
        return raw;
    }

    static float displayIntensity(uint16_t physical,
                                  const SonarDisplayParams& params) noexcept
    {
        return displayIntensity(static_cast<float>(physical) / 65535.f, params);
    }

    static float displayIntensity(float v,
                                  const SonarDisplayParams& params) noexcept
    {
        const float lo = params.display_low;
        const float hi = params.display_high;
        if (hi > lo + 1.f / 65535.f) {
            v = (v - lo) / (hi - lo);
            v = std::clamp(v, 0.f, 1.f);
        }

        if (params.gain > 0.f)
            v = std::pow(v, 1.f / params.gain);

        v = (v - 0.5f) * params.contrast + 0.5f;

        const float thr = std::clamp(params.threshold, 0.f, 0.99f);
        if (v < thr) v = 0.f;
        else         v = (v - thr) / (1.f - thr);

        return std::clamp(v, 0.f, 1.f);
    }
};

} // namespace dolphin::ui
