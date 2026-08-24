#include "ui/features/map/sidescan/SidescanRasterFingerprint.h"
#include "pipeline/SidescanEnhancementAlgorithms.h"
#include "pipeline/SidescanRadiometryAlgorithms.h"
#include <cmath>

namespace dolphin::ui::rastercache::detail {
namespace {
constexpr unsigned long long kOffset = 1469598103934665603ULL;
constexpr unsigned long long kPrime = 1099511628211ULL;

template <typename T>
unsigned long long mix(unsigned long long hash, const T& value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (size_t i = 0; i < sizeof(T); ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }
    return hash;
}

unsigned long long mixFloat(unsigned long long hash, double value)
{
    return mix(hash, static_cast<long long>(std::llround(value * 10000.0)));
}
} // namespace

unsigned long long makeRasterFingerprint(
    const NavProcessingParams& nav, const SssGeorefParams& georef,
    const std::string& display_crs_id, const WaterfallParams& sss)
{
    auto h = kOffset;
    h = mix(h, nav.smooth_enabled); h = mix(h, nav.smooth_window);
    h = mix(h, nav.layback_enabled); h = mixFloat(h, nav.layback_m);
    h = mixFloat(h, nav.heading_offset_deg); h = mixFloat(h, nav.pitch_offset_deg);
    h = mixFloat(h, nav.roll_offset_deg);
    h = mix(h, static_cast<int>(georef.nav_source));
    h = mix(h, static_cast<int>(georef.heading_source));
    h = mix(h, georef.enable_layback); h = mix(h, georef.use_file_layback);
    h = mixFloat(h, georef.manual_layback_m); h = mixFloat(h, georef.x_offset_m);
    h = mixFloat(h, georef.y_offset_m); h = mixFloat(h, georef.heading_offset_deg);
    h = mix(h, georef.swap_port_starboard);
    h = mix(h, static_cast<int>(georef.smoothing_mode));
    h = mix(h, georef.smoothing_window); h = mix(h, georef.debug_ping_lines_only);
    h = mix(h, georef.presentation_domain);
    for (char c : display_crs_id) h = mix(h, c);
    h = mix(h, pipeline::enhancement::kAlgorithmRevision);
    h = mix(h, pipeline::radiometry::kAlgorithmRevision);
    h = mix(h, sss.tvg.enabled); h = mixFloat(h, sss.tvg.spreading);
    h = mixFloat(h, sss.tvg.absorption); h = mixFloat(h, sss.tvg.fallback_blanking_m);
    h = mix(h, sss.arc.enabled); h = mixFloat(h, sss.arc.exponent);
    h = mixFloat(h, sss.arc.gain_cap_db); h = mix(h, sss.agc.enabled);
    h = mix(h, static_cast<int>(sss.agc.mode)); h = mixFloat(h, sss.agc.strength);
    h = mix(h, sss.agc.along_track_win); h = mix(h, static_cast<int>(sss.agc.smoothing_type));
    h = mix(h, sss.agc.smoothing_win); h = mix(h, sss.agc.edge_skip_samples);
    h = mixFloat(h, sss.agc.noise_floor_pct); h = mixFloat(h, sss.agc.gain_cap_db);
    h = mixFloat(h, sss.agc.target_mean); h = mix(h, sss.arn.enabled);
    h = mixFloat(h, sss.arn.strength); h = mixFloat(h, sss.arn.gain_cap_db);
    h = mix(h, sss.arn.column_smooth); h = mix(h, sss.destripe.enabled);
    h = mix(h, sss.destripe.window); h = mix(h, sss.destripe.subdivision);
    h = mixFloat(h, sss.destripe.capping); h = mixFloat(h, sss.destripe.threshold_db);
    h = mix(h, sss.beam_pattern.enabled); h = mixFloat(h, sss.beam_pattern.strength);
    h = mix(h, sss.beam_pattern.smooth_radius); h = mixFloat(h, sss.beam_pattern.gain_cap_db);
    h = mix(h, sss.ml_enhance.enabled); h = mix(h, sss.ml_enhance.tile_pings);
    h = mix(h, sss.ml_enhance.tile_samps); h = mixFloat(h, sss.ml_enhance.clip_limit);
    return h;
}

} // namespace dolphin::ui::rastercache::detail
