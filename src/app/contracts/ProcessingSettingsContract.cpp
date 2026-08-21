#include "app/contracts/ProcessingSettingsContract.h"

#include <cmath>

namespace dolphin::app::contracts {
namespace {

template<typename T>
bool finiteIn(T value, T low, T high)
{
    return std::isfinite(static_cast<double>(value)) && value >= low && value <= high;
}

} // namespace

std::string validate(const dolphin::ui::WaterfallParams& p)
{
    if (!finiteIn(p.gain, 0.01f, 100.f)
            || !finiteIn(p.contrast, 0.01f, 20.f)
            || !finiteIn(p.threshold, 0.f, 1.f)
            || !finiteIn(p.smoothing, 0.f, 100.f)
            || !finiteIn(p.display_low, 0.f, 1.f)
            || !finiteIn(p.display_high, 0.f, 1.f)
            || p.display_high < p.display_low
            || p.palette < dolphin::ui::PaletteIndex::Thermal
            || p.palette >= dolphin::ui::PaletteIndex::Count)
        return "sidescan display parameters are invalid";
    if (!finiteIn(p.tvg.spreading, 0.f, 40.f)
            || !finiteIn(p.tvg.absorption, 0.f, 2.f)
            || !finiteIn(p.tvg.fallback_blanking_m, 0.f, 50.f))
        return "TVG parameters are outside the supported physical range";
    if (!finiteIn(p.agc.strength, 0.f, 1.f)
            || p.agc.along_track_win < 1 || p.agc.along_track_win > 5000
            || p.agc.smoothing_win < 1 || p.agc.smoothing_win > 500
            || p.agc.edge_skip_samples < 0 || p.agc.edge_skip_samples > 100000
            || !finiteIn(p.agc.noise_floor_pct, 0.f, 100.f)
            || !finiteIn(p.agc.gain_cap_db, 0.f, 60.f)
            || !finiteIn(p.agc.target_mean, 256.f, 65535.f)
            || (p.agc.mode != AgcMode::Global && p.agc.mode != AgcMode::Variable)
            || (p.agc.smoothing_type != AgcSmoothingType::Mean
                && p.agc.smoothing_type != AgcSmoothingType::Median))
        return "AGC parameters are invalid";
    if (!finiteIn(p.arc.exponent, 0.5f, 4.f)
            || !finiteIn(p.arc.gain_cap_db, 0.f, 40.f))
        return "ARC parameters are invalid";
    if (!finiteIn(p.arn.strength, 0.f, 1.f)
            || !finiteIn(p.arn.gain_cap_db, 0.f, 40.f)
            || p.arn.column_smooth < 0 || p.arn.column_smooth > 1000)
        return "ARN parameters are invalid";
    if (p.destripe.window < 1 || p.destripe.window > 5000
            || p.destripe.subdivision < 1 || p.destripe.subdivision > 64
            || !finiteIn(p.destripe.capping, 1.f, 10.f)
            || !finiteIn(p.destripe.threshold_db, 0.f, 12.f))
        return "destripe parameters are invalid";
    if (!finiteIn(p.beam_pattern.strength, 0.f, 1.f)
            || p.beam_pattern.smooth_radius < 0
            || p.beam_pattern.smooth_radius > 1000
            || !finiteIn(p.beam_pattern.gain_cap_db, 0.f, 40.f))
        return "beam-pattern parameters are invalid";
    if (p.ml_enhance.tile_pings < 16 || p.ml_enhance.tile_pings > 4096
            || p.ml_enhance.tile_samps < 16 || p.ml_enhance.tile_samps > 8192
            || !finiteIn(p.ml_enhance.clip_limit, 1.f, 16.f))
        return "adaptive enhancement parameters are invalid";
    return {};
}

std::string validate(const dolphin::ui::SubBottomDisplayParams& p)
{
    if (p.palette_index < dolphin::ui::PaletteIndex::Thermal
            || p.palette_index >= dolphin::ui::PaletteIndex::Count
            || !finiteIn(p.gain, 0.01f, 100.f)
            || !finiteIn(p.contrast, 0.01f, 20.f)
            || !finiteIn(p.sound_speed_ms, 1000.f, 2000.f))
        return "sub-bottom display parameters are invalid";
    return {};
}

std::string validate(const SidescanCorrectionParams& p)
{
    dolphin::ui::WaterfallParams complete;
    complete.tvg = p.tvg;
    complete.arc = p.arc;
    complete.agc = p.agc;
    return validate(complete);
}

std::string validate(const SbpGainParams& gain, const SbpSignalParams& signal)
{
    if (!finiteIn(gain.static_gain_db, -20.f, 20.f)
            || (gain.agc_en && (gain.agc_window < 1 || gain.agc_window > 5000))
            || !finiteIn(gain.agc_gain_cap_db, 0.f, 80.f))
        return "sub-bottom gain parameters are invalid";
    if (!finiteIn(signal.bp_lo_hz, 0.f, 1'000'000.f)
            || !finiteIn(signal.bp_hi_hz, 0.f, 1'000'000.f))
        return "sub-bottom bandpass frequencies are invalid";
    if (signal.bandpass_en && (!(signal.bp_lo_hz > 0.f)
            || !(signal.bp_hi_hz > signal.bp_lo_hz)))
        return "sub-bottom bandpass requires 0 < low cutoff < high cutoff";
    return {};
}

std::string validate(const dolphin::ui::NavProcessingParams& p)
{
    if (p.smooth_window < 1 || p.smooth_window > 10000)
        return "navigation smoothing window is invalid";
    if (!finiteIn(p.layback_m, 0.f, 100000.f)
            || !finiteIn(p.heading_offset_deg, -360.f, 360.f)
            || !finiteIn(p.pitch_offset_deg, -360.f, 360.f)
            || !finiteIn(p.roll_offset_deg, -360.f, 360.f))
        return "navigation correction parameters are invalid";
    return {};
}

} // namespace dolphin::app::contracts
