#include "app/corrections/CorrectionAlgorithms.h"
#include "core/SidescanGeometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dolphin::app::corrections {

namespace {

float sampleRangeMetres(const core::SidescanPing& ping, int index)
{
    const float stored = ping.samples[static_cast<size_t>(index)].range_m;
    if (std::isfinite(stored) && stored > 0.0f) return stored;
    const int count = static_cast<int>(ping.samples.size());
    const float blanking = std::max(0.0f, ping.blanking_m);
    return blanking + (ping.slant_range_m - blanking)
        * static_cast<float>(index) / static_cast<float>(count - 1);
}

std::pair<int, int> exactWindow(int center, int length, int count)
{
    const int window = std::max(1, length);
    const int left_extent = (window - 1) / 2;
    const int right_extent = window / 2;
    return {std::max(0, center - left_extent),
            std::min(count - 1, center + right_extent)};
}

} // namespace

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
            const float r = sampleRangeMetres(ping, i);
            if (r <= kRef) continue;

            const double gain_db = static_cast<double>(tvg.spreading) * std::log10(r / kRef)
                                 + static_cast<double>(tvg.absorption) * (r - kRef);
            const double factor = std::pow(10.0, gain_db / 20.0);
            ping.samples[i].amplitude = static_cast<uint16_t>(
                std::clamp(static_cast<double>(ping.samples[i].amplitude) * factor,
                           0.0, 65535.0));
        }
    }
}

void applyArc(std::vector<core::SidescanPing>& pings, const ArcParams& arc)
{
    if (!arc.enabled) return;

    for (auto& ping : pings) {
        const int ns = static_cast<int>(ping.samples.size());
        if (!(ns >= 2) || !(ping.slant_range_m > 0.f)) continue;

        const auto altitude_m = core::sidescanAltitudeMetres(ping);
        if (!altitude_m) continue;

        const float max_factor = std::pow(10.f, arc.gain_cap_db / 20.f);

        for (int i = 0; i < ns; ++i) {
            const float r = sampleRangeMetres(ping, i);
            if (r <= *altitude_m) continue;

            const float sin_theta = std::clamp(
                static_cast<float>(*altitude_m / r), 0.01f, 1.f);
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
    if (!agc.enabled) return;
    const float noise_thr = std::clamp(agc.noise_floor_pct, 0.0f, 100.0f)
        / 100.f * 65535.f;
    const float strength = std::clamp(agc.strength, 0.0f, 1.0f);
    constexpr float kTarget = 32767.5f;

    if (agc.mode == AgcMode::Global) {
        for (auto& ping : pings) {
            const int ns = static_cast<int>(ping.samples.size());
            if (ns == 0) continue;
            const int skip = std::clamp(agc.edge_skip_samples, 0, ns / 2);
            double sum = 0.0; size_t cnt = 0;
            for (int s = skip; s < ns - skip; ++s)
                if (ping.samples[s].amplitude > noise_thr) {
                    sum += ping.samples[s].amplitude; ++cnt;
                }
            if (cnt == 0 || sum < 1.0) continue;
            const double mean = sum / static_cast<double>(cnt);
            const double factor = 1.0 + (kTarget / mean - 1.0) * strength;
            for (auto& samp : ping.samples)
                samp.amplitude = static_cast<uint16_t>(
                    std::clamp(static_cast<double>(samp.amplitude) * factor,
                               0.0, 65535.0));
        }
    } else {
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

                double sum = 0.0; size_t cnt = 0;
                const auto [begin, end] = exactWindow(ci, agc.along_track_win, m);
                for (int cj = begin; cj <= end; ++cj) {
                    const auto& np  = pings[idx[cj]];
                    const int   nns = static_cast<int>(np.samples.size());
                    const int nskip = std::clamp(agc.edge_skip_samples, 0, nns / 2);
                    for (int s = nskip; s < nns - nskip; ++s)
                        if (np.samples[s].amplitude > noise_thr) {
                            sum += np.samples[s].amplitude; ++cnt;
                        }
                }
                if (cnt == 0 || sum < 1.0) continue;
                const float mean = static_cast<float>(sum / static_cast<double>(cnt));
                factors[static_cast<size_t>(ci)] =
                    1.f + (kTarget / mean - 1.f) * strength;
            }

            // The smoothing controls are part of the public AGC contract. Apply
            // them to the along-track gain curve, not to samples, so range detail
            // remains intact while abrupt ping-to-ping gain steps are suppressed.
            if (agc.smoothing_win > 1 && m > 1) {
                std::vector<float> smoothed(factors.size(), 1.f);
                std::vector<float> window;
                window.reserve(static_cast<size_t>(agc.smoothing_win));
                for (int ci = 0; ci < m; ++ci) {
                    const auto [begin, end] = exactWindow(ci, agc.smoothing_win, m);
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
