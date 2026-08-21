#pragma once

#include <cstdint>
#include <vector>

namespace dolphin::pipeline::enhancement {

// Increment whenever output-affecting enhancement math changes. Persistent
// line contexts and map mosaics include this in their cache fingerprints.
inline constexpr uint64_t kAlgorithmRevision = 3;

struct ArnSettings {
    bool enabled = false;
    float strength = 0.8f;
    float gain_cap_db = 12.f;
    int column_smooth = 5;
};

struct DestripeSettings {
    bool enabled = false;
    int window = 50;
    int subdivision = 4;
    float capping = 2.f;
    float threshold_db = 1.f;
};

struct BeamPatternSettings {
    bool enabled = false;
    float strength = 1.f;
    int smooth_radius = 10;
    float gain_cap_db = 12.f;
};

struct AdaptiveContrastSettings {
    bool enabled = false;
    int tile_pings = 64;
    int tile_samps = 128;
    float clip_limit = 2.f;
};

using AmplitudeRows = std::vector<std::vector<uint16_t>*>;

bool applyBeamPattern(AmplitudeRows& rows, const BeamPatternSettings& settings);
bool applyArn(AmplitudeRows& rows, const ArnSettings& settings);
bool applyDestripe(AmplitudeRows& rows, const DestripeSettings& settings);
bool applyAdaptiveContrast(AmplitudeRows& rows,
                           const AdaptiveContrastSettings& settings);

} // namespace dolphin::pipeline::enhancement
