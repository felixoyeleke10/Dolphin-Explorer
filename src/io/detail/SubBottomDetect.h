#pragma once
#include <algorithm>
#include <cmath>

namespace dolphin::io {

// Locate the first-return seabed pick in a normalised sub-bottom amplitude
// vector.  Returns the sample index of the leading edge, or -1 if no pick was
// found.
//
// Algorithm:
//   - Skip a blanking window (blanking_frac × n) to avoid the direct blast.
//   - Compute peak amplitude and noise RMS over the blanking window.
//   - Threshold = max(peak × threshold_frac, noise_rms × 4) — keeps the pick
//     above both a peak fraction and 12 dB above the noise floor so that
//     polarity reversals don't misplace the pick.
//   - Require 3 consecutive samples above threshold to reject single spikes.
inline int detectBottomSample(const float* samples, int n,
                               float blanking_frac  = 0.03f,
                               float threshold_frac = 0.15f)
{
    if (n <= 0) return -1;
    const int blanking = std::max(1, static_cast<int>(n * blanking_frac));
    if (blanking >= n - 2) return -1;

    float peak = 0.f;
    for (int i = blanking; i < n; ++i)
        peak = std::max(peak, std::abs(samples[i]));
    if (peak < 1e-6f) return -1;

    float noise_sum_sq = 0.f;
    for (int i = 0; i < blanking; ++i)
        noise_sum_sq += samples[i] * samples[i];
    const float noise_rms = std::sqrt(noise_sum_sq / static_cast<float>(blanking));

    const float thr = std::max(peak * threshold_frac, noise_rms * 4.f);

    for (int i = blanking; i < n - 2; ++i) {
        if (std::abs(samples[i])     >= thr
         && std::abs(samples[i + 1]) >= thr
         && std::abs(samples[i + 2]) >= thr)
            return i;
    }
    for (int i = n - 2; i < n; ++i)
        if (std::abs(samples[i]) >= thr) return i;

    return -1;
}

} // namespace dolphin::io
