#include "app/corrections/SubBottomCorrectionAlgorithms.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using namespace dolphin;

    std::vector<core::SubBottomTrace> traces(2);
    traces[0].samples = {2.0f};
    traces[0].correction_flags |= core::SbpCorrectionFlag::StaticGain;
    traces[1].samples = {2.0f};
    app::SbpGainParams gain;
    gain.static_gain_en = true;
    gain.static_gain_db = 6.020599913f;
    app::SbpSignalParams signal;
    assert(app::corrections::applySubBottomCorrections(traces, gain, signal));
    assert(std::abs(traces[0].samples[0] - 2.0f) < 1e-6f);
    assert(std::abs(traces[1].samples[0] - 4.0f) < 1e-5f);

    core::SubBottomTrace filtered;
    filtered.sample_rate_hz = 1000.0f;
    filtered.samples.resize(4000);
    for (size_t i = 0; i < filtered.samples.size(); ++i) {
        const double t = static_cast<double>(i) / filtered.sample_rate_hz;
        filtered.samples[i] = static_cast<float>(
            std::sin(2.0 * std::acos(-1.0) * 10.0 * t)
            + std::sin(2.0 * std::acos(-1.0) * 100.0 * t));
    }
    signal.bandpass_en = true;
    signal.bp_lo_hz = 50.0f;
    signal.bp_hi_hz = 150.0f;
    std::vector<core::SubBottomTrace> filtered_traces{filtered};
    assert(app::corrections::applySubBottomCorrections(filtered_traces, {}, signal));
    assert(core::hasSbpCorrectionFlag(filtered_traces[0].correction_flags,
                                      core::SbpCorrectionFlag::BandPass));

    // Ignore filter startup and measure projections onto the input sinusoids.
    double low_projection = 0.0;
    double pass_projection = 0.0;
    for (size_t i = 500; i < filtered.samples.size(); ++i) {
        const double t = static_cast<double>(i) / filtered.sample_rate_hz;
        const double value = filtered_traces[0].samples[i];
        low_projection += value * std::sin(2.0 * std::acos(-1.0) * 10.0 * t);
        pass_projection += value * std::sin(2.0 * std::acos(-1.0) * 100.0 * t);
    }
    assert(std::abs(pass_projection) > 20.0 * std::abs(low_projection));

    core::SubBottomTrace impulse;
    impulse.sample_rate_hz = 1000.0f;
    impulse.samples.assign(1001, 0.0f);
    impulse.samples[500] = 1.0f;
    std::vector<core::SubBottomTrace> impulse_traces{impulse};
    assert(app::corrections::applySubBottomCorrections(impulse_traces, {}, signal));
    const auto peak = std::max_element(impulse_traces[0].samples.begin(),
                                       impulse_traces[0].samples.end());
    assert(std::distance(impulse_traces[0].samples.begin(), peak) == 500);

    bool cancel = true;
    assert(!app::corrections::applySubBottomCorrections(
        filtered_traces, gain, signal, [&cancel] { return cancel; }));

    // A constant (DC) signal has an exact zero steady-state response through
    // a high-pass stage; a cold (zero-initial-state) filter pass starting
    // right at the true edge would show a real transient there instead. Edge
    // padding should settle that transient before the true data starts, so
    // the very first/last sample stays close to the true DC-zero response
    // rather than the padding-free cold-start value.
    core::SubBottomTrace dc_trace;
    dc_trace.sample_rate_hz = 1000.0f;
    dc_trace.samples.assign(60, 1.0f);
    std::vector<core::SubBottomTrace> dc_traces{dc_trace};
    assert(app::corrections::applySubBottomCorrections(dc_traces, {}, signal));
    assert(std::abs(dc_traces[0].samples.front()) < 0.1f);
    assert(std::abs(dc_traces[0].samples.back()) < 0.1f);

    // A very short trace (shorter than the padding window) must not crash —
    // padding clamps to size()-1 rather than reading/writing out of bounds.
    core::SubBottomTrace tiny;
    tiny.sample_rate_hz = 1000.0f;
    tiny.samples = {1.0f, 1.0f, 1.0f};
    std::vector<core::SubBottomTrace> tiny_traces{tiny};
    assert(app::corrections::applySubBottomCorrections(tiny_traces, {}, signal));

    std::cout << "Sub-bottom correction tests passed\n";
    return 0;
}
