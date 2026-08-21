#include "pipeline/SidescanRadiometryAlgorithms.h"

#include "core/SidescanGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <utility>

namespace dolphin::pipeline::radiometry {
namespace {

std::pair<int, int> exactWindow(int center, int length, int count)
{
    const int window = std::max(1, length);
    const int left = (window - 1) / 2;
    const int right = window / 2;
    return {std::max(0, center - left), std::min(count - 1, center + right)};
}

bool pingLess(const core::SidescanPing* left, const core::SidescanPing* right)
{
    if (left->timestamp_us != right->timestamp_us)
        return left->timestamp_us < right->timestamp_us;
    if (left->ping_number != right->ping_number)
        return left->ping_number < right->ping_number;
    return left->id < right->id;
}

} // namespace

bool canApplyArc(const core::SidescanPing& ping) noexcept
{
    if (ping.samples.size() < 2) return false;
    const auto altitude = core::sidescanAltitudeMetres(ping);
    if (!altitude) return false;
    for (size_t index = 0; index < ping.samples.size(); ++index) {
        const auto range = core::sidescanSampleRangeMetres(ping, index);
        if (range && *range > *altitude) return true;
    }
    return false;
}

bool applyTvg(std::vector<core::SidescanPing>& pings, const TvgSettings& input)
{
    if (!input.enabled) return false;
    const float spreading = std::clamp(
        std::isfinite(input.spreading) ? input.spreading : 0.f, 0.f, 40.f);
    const float absorption = std::clamp(
        std::isfinite(input.absorption) ? input.absorption : 0.f, 0.f, 2.f);
    const float fallback = std::clamp(
        std::isfinite(input.fallback_blanking_m) ? input.fallback_blanking_m : 1.f,
        0.f, 50.f);
    bool modified = false;
    for (auto& ping : pings) {
        if (core::hasCorrectionFlag(ping.correction_flags, core::CorrectionFlag::Tvg)
                || ping.samples.size() < 2 || !std::isfinite(ping.slant_range_m)
                || !(ping.slant_range_m > 0.f)) continue;
        const float blanking = std::isfinite(ping.blanking_m) && ping.blanking_m >= 0.f
            ? ping.blanking_m : fallback;
        const float reference = std::max(blanking, 1.f);
        const float span = ping.slant_range_m - blanking;
        if (!(span > 0.f)) continue;
        bool applied = false;
        for (size_t index = 0; index < ping.samples.size(); ++index) {
            auto& sample = ping.samples[index];
            const auto stored = core::sidescanSampleRangeMetres(ping, index);
            const double range = stored ? *stored
                : blanking + span * static_cast<double>(index)
                    / static_cast<double>(ping.samples.size() - 1);
            if (!(range > reference)) continue;
            const double gain_db = spreading * std::log10(range / reference)
                                 + absorption * (range - reference);
            const double factor = std::pow(10.0, gain_db / 20.0);
            const auto replacement = static_cast<uint16_t>(std::clamp(
                static_cast<double>(sample.amplitude) * factor, 0.0, 65535.0));
            applied |= replacement != sample.amplitude;
            sample.amplitude = replacement;
        }
        if (applied) {
            ping.correction_flags |= core::CorrectionFlag::Tvg;
            modified = true;
        }
    }
    return modified;
}

bool applyArc(std::vector<core::SidescanPing>& pings, const ArcSettings& input)
{
    if (!input.enabled) return false;
    const double exponent = std::clamp(
        std::isfinite(input.exponent) ? static_cast<double>(input.exponent) : 1.5,
        0.5, 4.0);
    const double cap_db = std::clamp(
        std::isfinite(input.gain_cap_db) ? static_cast<double>(input.gain_cap_db) : 12.0,
        0.0, 40.0);
    bool modified = false;
    for (auto& ping : pings) {
        if (core::hasCorrectionFlag(ping.correction_flags, core::CorrectionFlag::Arc))
            continue;
        const auto altitude = core::sidescanAltitudeMetres(ping);
        if (!altitude || ping.samples.size() < 2) continue;
        bool applied = false;
        for (size_t index = 0; index < ping.samples.size(); ++index) {
            const auto range = core::sidescanSampleRangeMetres(ping, index);
            if (!range) continue;
            const auto factor = core::angleRangeGainFactor(
                *range, *altitude, exponent, cap_db);
            if (!factor) continue;
            auto& sample = ping.samples[index];
            const auto replacement = static_cast<uint16_t>(std::clamp(
                static_cast<double>(sample.amplitude) * *factor, 0.0, 65535.0));
            applied |= replacement != sample.amplitude;
            sample.amplitude = replacement;
        }
        if (applied) {
            ping.correction_flags |= core::CorrectionFlag::Arc;
            modified = true;
        }
    }
    return modified;
}

bool applyAgc(std::vector<core::SidescanPing>& pings, const AgcSettings& input)
{
    if (!input.enabled) return false;
    const float noise_threshold = std::clamp(
        std::isfinite(input.noise_floor_pct) ? input.noise_floor_pct : 0.f,
        0.f, 100.f) * 0.01f * 65535.f;
    const double strength = std::clamp(
        std::isfinite(input.strength) ? static_cast<double>(input.strength) : 0.0,
        0.0, 1.0);
    const double cap = std::pow(10.0, std::clamp(
        std::isfinite(input.gain_cap_db) ? static_cast<double>(input.gain_cap_db) : 0.0,
        0.0, 60.0) / 20.0);
    const double target = std::clamp(
        std::isfinite(input.target_mean) ? static_cast<double>(input.target_mean) : 32767.5,
        256.0, 65535.0);
    const int edge_skip = std::clamp(input.edge_skip_samples, 0, 100000);
    const auto gainFor = [&](double mean) {
        return std::pow(std::clamp(target / mean, 0.0, cap), strength);
    };
    const auto stats = [&](const core::SidescanPing& ping) {
        std::pair<double, uint64_t> result{0.0, 0};
        const int count = static_cast<int>(ping.samples.size());
        const int skip = std::clamp(edge_skip, 0, count / 2);
        for (int index = skip; index < count - skip; ++index) {
            const auto amplitude = ping.samples[static_cast<size_t>(index)].amplitude;
            if (amplitude > noise_threshold) { result.first += amplitude; ++result.second; }
        }
        return result;
    };

    bool modified = false;
    for (const auto channel : {core::SidescanChannel::Port,
                               core::SidescanChannel::Starboard}) {
        std::vector<core::SidescanPing*> channel_pings;
        for (auto& ping : pings)
            if (ping.channel == channel && !core::hasCorrectionFlag(
                    ping.correction_flags, core::CorrectionFlag::GainNormalized))
                channel_pings.push_back(&ping);
        std::sort(channel_pings.begin(), channel_pings.end(), pingLess);
        if (channel_pings.empty()) continue;

        std::vector<float> factors(channel_pings.size(), 1.f);
        if (input.mode == AgcMode::Global) {
            // "Global" describes the across-sample statistic used for each
            // ping. It must not collapse an entire channel to one gain: doing
            // so preserves along-track level jumps instead of normalising them.
            for (size_t row = 0; row < channel_pings.size(); ++row) {
                const auto [sum, count] = stats(*channel_pings[row]);
                if (count > 0 && sum > 0.0)
                    factors[row] = static_cast<float>(gainFor(sum / count));
            }
        } else {
            const size_t count = channel_pings.size();
            std::vector<double> prefix_sum(count + 1, 0.0);
            std::vector<uint64_t> prefix_count(count + 1, 0);
            for (size_t row = 0; row < count; ++row) {
                const auto sample_stats = stats(*channel_pings[row]);
                prefix_sum[row + 1] = prefix_sum[row] + sample_stats.first;
                prefix_count[row + 1] = prefix_count[row] + sample_stats.second;
            }
            const int along_window = std::clamp(input.along_track_win, 1, 5000);
            for (int row = 0; row < static_cast<int>(count); ++row) {
                const auto [begin, end] = exactWindow(row, along_window,
                                                       static_cast<int>(count));
                const double sum = prefix_sum[static_cast<size_t>(end + 1)]
                                 - prefix_sum[static_cast<size_t>(begin)];
                const uint64_t hits = prefix_count[static_cast<size_t>(end + 1)]
                                    - prefix_count[static_cast<size_t>(begin)];
                if (hits > 0 && sum > 0.0)
                    factors[static_cast<size_t>(row)] = static_cast<float>(gainFor(sum / hits));
            }
            const int smooth_window = std::clamp(input.smoothing_win, 1, 500);
            if (smooth_window > 1 && count > 1) {
                std::vector<float> filtered(count, 1.f), window;
                window.reserve(static_cast<size_t>(smooth_window));
                for (int row = 0; row < static_cast<int>(count); ++row) {
                    const auto [begin, end] = exactWindow(
                        row, smooth_window, static_cast<int>(count));
                    if (input.smoothing_type == AgcSmoothingType::Median) {
                        window.assign(factors.begin() + begin, factors.begin() + end + 1);
                        const size_t middle = window.size() / 2;
                        std::nth_element(window.begin(), window.begin() + middle, window.end());
                        filtered[static_cast<size_t>(row)] = window[middle];
                    } else {
                        filtered[static_cast<size_t>(row)] = std::accumulate(
                            factors.begin() + begin, factors.begin() + end + 1, 0.f)
                            / static_cast<float>(end - begin + 1);
                    }
                }
                factors = std::move(filtered);
            }
        }
        for (size_t row = 0; row < channel_pings.size(); ++row) {
            auto& ping = *channel_pings[row];
            bool ping_modified = false;
            for (auto& sample : ping.samples) {
                const auto replacement = static_cast<uint16_t>(std::clamp(
                    static_cast<double>(sample.amplitude) * factors[row], 0.0, 65535.0));
                ping_modified |= replacement != sample.amplitude;
                sample.amplitude = replacement;
            }
            if (ping_modified) {
                ping.correction_flags |= core::CorrectionFlag::GainNormalized;
                modified = true;
            }
        }
    }
    return modified;
}

} // namespace dolphin::pipeline::radiometry
