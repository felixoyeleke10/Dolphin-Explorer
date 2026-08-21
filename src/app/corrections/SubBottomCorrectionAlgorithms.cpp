#include "app/corrections/SubBottomCorrectionAlgorithms.h"

#include <algorithm>
#include <cmath>

namespace dolphin::app::corrections {
namespace {

struct Biquad {
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

Biquad butterworth(double cutoff_hz, double sample_rate_hz, bool high_pass)
{
    constexpr double kInvSqrt2 = 0.70710678118654752440;
    const double omega = 2.0 * std::acos(-1.0) * cutoff_hz / sample_rate_hz;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) * kInvSqrt2;
    const double a0 = 1.0 + alpha;
    Biquad result;
    if (high_pass) {
        result.b0 = (1.0 + cosine) * 0.5 / a0;
        result.b1 = -(1.0 + cosine) / a0;
        result.b2 = result.b0;
    } else {
        result.b0 = (1.0 - cosine) * 0.5 / a0;
        result.b1 = (1.0 - cosine) / a0;
        result.b2 = result.b0;
    }
    result.a1 = -2.0 * cosine / a0;
    result.a2 = (1.0 - alpha) / a0;
    return result;
}

void filter(std::vector<float>& samples, const Biquad& c)
{
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (float& value : samples) {
        const double x0 = value;
        const double y0 = c.b0 * x0 + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = x0; y2 = y1; y1 = y0;
        value = static_cast<float>(y0);
    }
}

// Odd-reflection edge padding (matches scipy.signal.filtfilt's default
// padtype="odd"): extends both ends of the trace so the biquad's zero
// initial state settles against a continuous, sign-flipped mirror of the
// real data rather than an artificial hard edge at zero.
std::vector<float> padOdd(const std::vector<float>& samples, size_t pad_len)
{
    const size_t n = samples.size();
    pad_len = std::min(pad_len, n > 0 ? n - 1 : size_t(0));
    std::vector<float> out;
    out.reserve(n + 2 * pad_len);
    for (size_t k = pad_len; k >= 1; --k)
        out.push_back(2.0f * samples[0] - samples[std::min(k, n - 1)]);
    out.insert(out.end(), samples.begin(), samples.end());
    for (size_t k = 1; k <= pad_len; ++k)
        out.push_back(2.0f * samples[n - 1] - samples[n - 1 - std::min(k, n - 1)]);
    return out;
}

void filterZeroPhase(std::vector<float>& samples, const Biquad& coefficients)
{
    if (samples.size() < 2) { filter(samples, coefficients); return; }
    // Padding gives the zero-initial-state biquad room to settle against
    // real (mirrored) data before the first/last true sample, instead of
    // ringing against an implicit hard edge at zero. Without it, that
    // ringing lands exactly where shallow reflectors are — the opposite of
    // this filter's purpose of not displacing reflector positions.
    constexpr size_t kPadSamples = 12;
    const size_t pad = std::min(kPadSamples, samples.size() - 1);
    std::vector<float> padded = padOdd(samples, pad);
    filter(padded, coefficients);
    std::reverse(padded.begin(), padded.end());
    filter(padded, coefficients);
    std::reverse(padded.begin(), padded.end());
    samples.assign(padded.begin() + static_cast<long>(pad),
                   padded.begin() + static_cast<long>(pad + samples.size()));
}

bool isCancelled(const std::function<bool()>& cancelled)
{
    return cancelled && cancelled();
}

template<typename Transform>
void applyTracePass(std::vector<core::SubBottomTrace>& traces,
                    core::SbpCorrectionFlag flag, bool enabled,
                    Transform&& transform)
{
    if (!enabled) return;
    for (auto& trace : traces) {
        if (core::hasSbpCorrectionFlag(trace.correction_flags, flag)) continue;
        if (!trace.samples.empty() && transform(trace.samples))
            trace.correction_flags |= flag;
    }
}

} // namespace

bool applySubBottomCorrections(std::vector<core::SubBottomTrace>& traces,
                               const SbpGainParams& gain,
                               const SbpSignalParams& signal,
                               const std::function<bool()>& cancelled)
{
    applyTracePass(traces, core::SbpCorrectionFlag::DcRemoval, signal.dc_removal_en,
        [](std::vector<float>& samples) -> bool {
            double sum = 0.0;
            for (const float value : samples) sum += value;
            const double mean = sum / static_cast<double>(samples.size());
            if (!std::isfinite(mean) || mean == 0.0) return false;
            for (float& value : samples) value = static_cast<float>(value - mean);
            return true;
        });
    if (isCancelled(cancelled)) return false;

    if (signal.bandpass_en) {
        for (auto& trace : traces) {
            if (core::hasSbpCorrectionFlag(trace.correction_flags,
                                           core::SbpCorrectionFlag::BandPass))
                continue;
            const double sample_rate = trace.sample_rate_hz;
            const double nyquist = sample_rate * 0.5;
            if (!(sample_rate > 0.0) || !(signal.bp_lo_hz > 0.0f)
                || !(signal.bp_hi_hz > signal.bp_lo_hz)
                || !(signal.bp_hi_hz < nyquist))
                continue;
            // Forward-backward application removes IIR phase delay so reflector
            // positions remain aligned with picks. Each pass uses bilinear-
            // transform second-order Butterworth high/low sections.
            const auto before = trace.samples;
            filterZeroPhase(trace.samples,
                            butterworth(signal.bp_lo_hz, sample_rate, true));
            filterZeroPhase(trace.samples,
                            butterworth(signal.bp_hi_hz, sample_rate, false));
            if (trace.samples != before)
                trace.correction_flags |= core::SbpCorrectionFlag::BandPass;
        }
    }
    if (isCancelled(cancelled)) return false;

    // This is full-wave rectification. A true analytic-signal envelope needs a
    // Hilbert transform and is deliberately not claimed by this operation.
    applyTracePass(traces, core::SbpCorrectionFlag::Envelope, signal.envelope_en,
        [](std::vector<float>& samples) -> bool {
            bool modified = false;
            for (const float value : samples) modified |= value < 0.f;
            if (!modified) return false;
            for (float& value : samples) value = std::abs(value);
            return true;
        });
    if (isCancelled(cancelled)) return false;

    applyTracePass(traces, core::SbpCorrectionFlag::Normalize, gain.normalize_en,
        [](std::vector<float>& samples) -> bool {
            float peak = 0.0f;
            for (const float value : samples) peak = std::max(peak, std::abs(value));
            if (!(peak > 0.0f) || !std::isfinite(peak) || peak == 1.0f)
                return false;
            for (float& value : samples) value /= peak;
            return true;
        });
    if (isCancelled(cancelled)) return false;

    const bool static_gain_enabled = gain.static_gain_en
        && std::isfinite(gain.static_gain_db) && gain.static_gain_db != 0.0f;
    const double gain_factor = static_gain_enabled
        ? std::pow(10.0, static_cast<double>(gain.static_gain_db) / 20.0) : 1.0;
    applyTracePass(traces, core::SbpCorrectionFlag::StaticGain, static_gain_enabled,
        [gain_factor](std::vector<float>& samples) -> bool {
            bool modified = false;
            for (float& value : samples) {
                if (!std::isfinite(value) || value == 0.f) continue;
                const float gained = static_cast<float>(
                    static_cast<double>(value) * gain_factor);
                modified |= gained != value;
                value = gained;
            }
            return modified;
        });
    if (isCancelled(cancelled)) return false;

    if (gain.agc_en) {
        const int trace_count = static_cast<int>(traces.size());
        const int half_window = std::max(0, gain.agc_window);
        std::vector<double> energy(static_cast<size_t>(trace_count), 0.0);
        std::vector<size_t> sample_count(static_cast<size_t>(trace_count), 0);
        for (int i = 0; i < trace_count; ++i) {
            if (core::hasSbpCorrectionFlag(
                    traces[static_cast<size_t>(i)].correction_flags,
                    core::SbpCorrectionFlag::Agc))
                continue;
            for (const float value : traces[static_cast<size_t>(i)].samples) {
                if (!std::isfinite(value)) continue;
                const double sample = value;
                energy[static_cast<size_t>(i)] += sample * sample;
                ++sample_count[static_cast<size_t>(i)];
            }
        }

        double window_energy = 0.0;
        size_t window_samples = 0;
        int left = 0;
        int right = -1;
        const double max_gain = std::pow(10.0,
            static_cast<double>(std::clamp(gain.agc_gain_cap_db, 0.f, 80.f)) / 20.0);
        for (int i = 0; i < trace_count; ++i) {
            if (isCancelled(cancelled)) return false;
            const int desired_right = std::min(trace_count - 1, i + half_window);
            while (right < desired_right) {
                ++right;
                window_energy += energy[static_cast<size_t>(right)];
                window_samples += sample_count[static_cast<size_t>(right)];
            }
            const int desired_left = std::max(0, i - half_window);
            while (left < desired_left) {
                window_energy -= energy[static_cast<size_t>(left)];
                window_samples -= sample_count[static_cast<size_t>(left)];
                ++left;
            }

            auto& trace = traces[static_cast<size_t>(i)];
            if (core::hasSbpCorrectionFlag(trace.correction_flags,
                                           core::SbpCorrectionFlag::Agc))
                continue;
            if (window_samples > 0 && window_energy > 0.0) {
                const double rms = std::sqrt(window_energy / static_cast<double>(window_samples));
                const double factor = std::min(1.0 / rms, max_gain);
                bool modified = false;
                if (std::isfinite(factor) && factor > 0.0 && factor != 1.0) {
                    for (float& value : trace.samples)
                        if (std::isfinite(value)) {
                            value = static_cast<float>(static_cast<double>(value) * factor);
                            modified = true;
                        }
                }
                if (modified)
                    trace.correction_flags |= core::SbpCorrectionFlag::Agc;
            }
        }
    }
    return true;
}

} // namespace dolphin::app::corrections
