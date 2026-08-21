#include "app/corrections/SubBottomCorrectionAlgorithms.h"
#include "app/contracts/ProcessingSettingsContract.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    using namespace dolphin;

    // Every execution/persistence entry point shares these validation rules.
    assert(app::contracts::validate(ui::WaterfallParams{}).empty());
    assert(app::contracts::validate(ui::SubBottomDisplayParams{}).empty());
    assert(app::contracts::validate(app::SbpGainParams{}, app::SbpSignalParams{}).empty());
    assert(app::contracts::validate(ui::NavProcessingParams{}).empty());
    app::SbpGainParams disabled_agc;
    disabled_agc.agc_en = false;
    disabled_agc.agc_window = 999999; // irrelevant while disabled
    assert(app::contracts::validate(disabled_agc, app::SbpSignalParams{}).empty());

    ui::WaterfallParams bad_sss;
    bad_sss.agc.strength = std::numeric_limits<float>::quiet_NaN();
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.agc.gain_cap_db = 100.f;
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.agc.mode = static_cast<app::AgcMode>(99);
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.arc.exponent = std::numeric_limits<float>::quiet_NaN();
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.arc.gain_cap_db = 100.f;
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.arn.strength = std::numeric_limits<float>::quiet_NaN();
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.arn.column_smooth = -1;
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.destripe.capping = 0.5f;
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.destripe.threshold_db = std::numeric_limits<float>::quiet_NaN();
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.beam_pattern.gain_cap_db = std::numeric_limits<float>::infinity();
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.ml_enhance.tile_pings = 0;
    assert(!app::contracts::validate(bad_sss).empty());
    bad_sss = {};
    bad_sss.ml_enhance.clip_limit = std::numeric_limits<float>::quiet_NaN();
    assert(!app::contracts::validate(bad_sss).empty());
    ui::SubBottomDisplayParams bad_display;
    bad_display.sound_speed_ms = 0.f;
    assert(!app::contracts::validate(bad_display).empty());
    app::SbpSignalParams bad_signal;
    bad_signal.bandpass_en = true;
    bad_signal.bp_lo_hz = 500.f;
    bad_signal.bp_hi_hz = 100.f;
    assert(!app::contracts::validate(app::SbpGainParams{}, bad_signal).empty());
    app::SbpGainParams bad_sbp_gain;
    bad_sbp_gain.agc_gain_cap_db = 100.f;
    assert(!app::contracts::validate(bad_sbp_gain, {}).empty());
    ui::NavProcessingParams bad_nav;
    bad_nav.smooth_window = 0;
    assert(!app::contracts::validate(bad_nav).empty());

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

    // Running-RMS AGC is bounded and ignores already-corrected traces when
    // estimating a mixed legacy store's remaining gains.
    app::SbpGainParams capped_agc;
    capped_agc.agc_en = true;
    capped_agc.agc_window = 5;
    capped_agc.agc_gain_cap_db = 20.f;
    core::SubBottomTrace weak;
    weak.samples = {1.0e-6f};
    std::vector<core::SubBottomTrace> weak_traces{weak};
    assert(app::corrections::applySubBottomCorrections(
        weak_traces, capped_agc, {}));
    assert(std::abs(weak_traces[0].samples[0] - 1.0e-5f) < 1.0e-8f);

    core::SubBottomTrace baked_agc;
    baked_agc.samples = {1.f};
    baked_agc.correction_flags |= core::SbpCorrectionFlag::Agc;
    core::SubBottomTrace raw_agc;
    raw_agc.samples = {2.f};
    std::vector<core::SubBottomTrace> mixed_agc{baked_agc, raw_agc};
    capped_agc.agc_gain_cap_db = 40.f;
    assert(app::corrections::applySubBottomCorrections(
        mixed_agc, capped_agc, {}));
    assert(std::abs(mixed_agc[0].samples[0] - 1.f) < 1e-6f);
    assert(std::abs(mixed_agc[1].samples[0] - 1.f) < 1e-6f);

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

    // Empty/identity inputs do not acquire provenance merely because a control
    // was enabled.
    app::SbpGainParams identity_gain;
    identity_gain.normalize_en = true;
    core::SubBottomTrace zero_trace;
    zero_trace.samples.assign(16, 0.f);
    std::vector<core::SubBottomTrace> zero_traces{zero_trace};
    assert(app::corrections::applySubBottomCorrections(
        zero_traces, identity_gain, {}));
    assert(!core::hasSbpCorrectionFlag(
        zero_traces.front().correction_flags,
        core::SbpCorrectionFlag::Normalize));

    std::cout << "Sub-bottom correction tests passed\n";
    return 0;
}
