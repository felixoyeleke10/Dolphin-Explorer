#include "app/corrections/CorrectionAlgorithms.h"

#include <algorithm>
#include <cmath>

namespace dolphin::app::corrections {

void applyTvg(std::vector<core::SidescanPing>& pings, const TvgParams& tvg)
{
    if (!tvg.enabled) return;

    for (auto& ping : pings) {
        const int ns = static_cast<int>(ping.samples.size());
        if (!(ns >= 2) || !(ping.slant_range_m > 0.f)) continue;

        const float blanking_m = std::max(0.f, ping.blanking_m);
        const float kRef = std::max(1.f, blanking_m);
        const float span = ping.slant_range_m - blanking_m;
        if (span <= 0.f) continue;

        for (int i = 0; i < ns; ++i) {
            const float r = blanking_m
                          + span * static_cast<float>(i) / (ns > 1 ? ns - 1 : 1);
            if (r <= kRef) continue;

            const float gain_db = tvg.spreading  * std::log10(r / kRef)
                                + tvg.absorption * (r - kRef);
            const float factor = std::pow(10.f, gain_db / 20.f);
            ping.samples[i].amplitude = static_cast<uint16_t>(
                std::clamp(static_cast<float>(ping.samples[i].amplitude) * factor,
                           0.f, 65535.f));
        }
    }
}

void applyArc(std::vector<core::SidescanPing>& pings, const ArcParams& arc)
{
    if (!arc.enabled) return;

    for (auto& ping : pings) {
        const int ns = static_cast<int>(ping.samples.size());
        if (!(ns >= 2) || !(ping.slant_range_m > 0.f)) continue;

        const float h = ping.bottom_pick.valid() ? ping.bottom_pick.range_m
                                                 : ping.tow_depth_m;
        if (h <= 0.f) continue;

        const float span       = ping.slant_range_m - ping.blanking_m;
        const float max_factor = std::pow(10.f, arc.gain_cap_db / 20.f);

        for (int i = 0; i < ns; ++i) {
            const float r = ping.blanking_m
                          + span * static_cast<float>(i) / (ns > 1 ? ns - 1 : 1);
            if (r <= h) continue;

            const float sin_theta = std::clamp(h / r, 0.01f, 1.f);
            const float factor    = std::min(1.f / std::pow(sin_theta, arc.exponent),
                                             max_factor);
            ping.samples[i].amplitude = static_cast<uint16_t>(
                std::clamp(static_cast<float>(ping.samples[i].amplitude) * factor,
                           0.f, 65535.f));
        }
    }
}

void normalizeAmplitudes(std::vector<core::SidescanPing>& pings, const AgcParams& agc)
{
    const float noise_thr = agc.noise_floor_pct / 100.f * 65535.f;
    constexpr float kTarget = 32767.5f;

    if (agc.mode == AgcMode::Global) {
        for (auto& ping : pings) {
            const int ns = static_cast<int>(ping.samples.size());
            if (ns == 0) continue;
            const int skip = std::min(agc.edge_skip_samples, ns / 2);
            float sum = 0.f; int cnt = 0;
            for (int s = skip; s < ns - skip; ++s)
                if (ping.samples[s].amplitude > noise_thr) {
                    sum += ping.samples[s].amplitude; ++cnt;
                }
            if (cnt == 0 || sum < 1.f) continue;
            const float mean   = sum / cnt;
            const float factor = 1.f + (kTarget / mean - 1.f) * agc.strength;
            for (auto& samp : ping.samples)
                samp.amplitude = static_cast<uint16_t>(
                    std::clamp(static_cast<float>(samp.amplitude) * factor,
                               0.f, 65535.f));
        }
    } else {
        const int   half_win = std::max(0, agc.along_track_win / 2);
        const int   n        = static_cast<int>(pings.size());

        std::vector<int> port_idx, stbd_idx;
        for (int i = 0; i < n; ++i) {
            if (pings[i].channel == core::SidescanChannel::Port) port_idx.push_back(i);
            else                                                  stbd_idx.push_back(i);
        }

        auto normalizeChannel = [&](const std::vector<int>& idx) {
            const int m = static_cast<int>(idx.size());
            if (m == 0) return;
            std::vector<float> factors(static_cast<size_t>(m), 1.f);
            for (int ci = 0; ci < m; ++ci) {
                const auto& ping = pings[idx[ci]];
                const int ns = static_cast<int>(ping.samples.size());
                if (ns == 0) continue;

                double sum = 0.0; int cnt = 0;
                for (int cj = std::max(0, ci - half_win);
                         cj <= std::min(m - 1, ci + half_win); ++cj) {
                    const auto& np  = pings[idx[cj]];
                    const int   nns = static_cast<int>(np.samples.size());
                    const int nskip = std::min(agc.edge_skip_samples, nns / 2);
                    for (int s = nskip; s < nns - nskip; ++s)
                        if (np.samples[s].amplitude > noise_thr) {
                            sum += np.samples[s].amplitude; ++cnt;
                        }
                }
                if (cnt == 0 || sum < 1.0) continue;
                const float mean = static_cast<float>(sum / cnt);
                factors[static_cast<size_t>(ci)] =
                    1.f + (kTarget / mean - 1.f) * agc.strength;
            }

            // The smoothing controls are part of the public AGC contract. Apply
            // them to the along-track gain curve, not to samples, so range detail
            // remains intact while abrupt ping-to-ping gain steps are suppressed.
            if (agc.smoothing_win > 1 && m > 1) {
                const int smooth_half = std::max(1, agc.smoothing_win / 2);
                std::vector<float> smoothed(factors.size(), 1.f);
                std::vector<float> window;
                window.reserve(static_cast<size_t>(smooth_half * 2 + 1));
                for (int ci = 0; ci < m; ++ci) {
                    const int begin = std::max(0, ci - smooth_half);
                    const int end = std::min(m - 1, ci + smooth_half);
                    if (agc.smoothing_type == AgcSmoothingType::Median) {
                        window.assign(factors.begin() + begin,
                                      factors.begin() + end + 1);
                        const size_t middle = window.size() / 2;
                        std::nth_element(window.begin(), window.begin() + middle,
                                         window.end());
                        smoothed[static_cast<size_t>(ci)] = window[middle];
                    } else {
                        double gain_sum = 0.0;
                        for (int i = begin; i <= end; ++i)
                            gain_sum += factors[static_cast<size_t>(i)];
                        smoothed[static_cast<size_t>(ci)] = static_cast<float>(
                            gain_sum / static_cast<double>(end - begin + 1));
                    }
                }
                factors = std::move(smoothed);
            }

            for (int ci = 0; ci < m; ++ci) {
                auto& ping = pings[idx[ci]];
                const float factor = factors[static_cast<size_t>(ci)];
                for (auto& samp : ping.samples)
                    samp.amplitude = static_cast<uint16_t>(
                        std::clamp(static_cast<float>(samp.amplitude) * factor,
                                   0.f, 65535.f));
            }
        };

        normalizeChannel(port_idx);
        normalizeChannel(stbd_idx);
    }
}

} // namespace dolphin::app::corrections
