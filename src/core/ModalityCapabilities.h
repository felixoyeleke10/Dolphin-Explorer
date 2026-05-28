#pragma once
#include "core/Artifact.h"

namespace dolphin::core {

// Per-modality capability flags.
// UI and service code should query these instead of scattering
// `if (type == Sidescan)` guards.  Add new modalities by adding a case in
// capabilitiesFor() — all call sites that check a specific flag will
// automatically light up for the new modality if it sets that flag.
struct ModalityCapabilities {
    bool hasMapFootprint;    // traces a geographic path (lat/lon per record)
    bool hasWaterfallView;   // can be rendered as a scrolling amplitude waterfall
    bool hasProfileView;     // has a depth/time axis (two-way-time, vertical section)
    bool hasFrequencyBands;  // supports dual/multi-frequency splits
    bool hasPortStarboard;   // paired port+starboard channels
    bool hasBottomTrack;     // carries a seabed first-return index or range
    bool hasTraceSamples;    // payload is a float[] amplitude trace
    bool hasMagSamples;      // payload is scalar magnetic field components
};

inline ModalityCapabilities capabilitiesFor(ArtifactType type)
{
    switch (type) {
    case ArtifactType::Sidescan:
        return { .hasMapFootprint   = true,
                 .hasWaterfallView  = true,
                 .hasProfileView    = false,
                 .hasFrequencyBands = true,
                 .hasPortStarboard  = true,
                 .hasBottomTrack    = true,
                 .hasTraceSamples   = true,
                 .hasMagSamples     = false };

    case ArtifactType::SubBottom:
        return { .hasMapFootprint   = true,
                 .hasWaterfallView  = true,
                 .hasProfileView    = true,
                 .hasFrequencyBands = false,
                 .hasPortStarboard  = false,
                 .hasBottomTrack    = true,
                 .hasTraceSamples   = true,
                 .hasMagSamples     = false };

    case ArtifactType::Magnetometer:
        return { .hasMapFootprint   = true,
                 .hasWaterfallView  = false,
                 .hasProfileView    = true,
                 .hasFrequencyBands = false,
                 .hasPortStarboard  = false,
                 .hasBottomTrack    = false,
                 .hasTraceSamples   = false,
                 .hasMagSamples     = true };

    case ArtifactType::Multibeam:
        return { .hasMapFootprint   = true,
                 .hasWaterfallView  = false,
                 .hasProfileView    = true,
                 .hasFrequencyBands = false,
                 .hasPortStarboard  = false,
                 .hasBottomTrack    = true,
                 .hasTraceSamples   = false,
                 .hasMagSamples     = false };

    case ArtifactType::Raster:
        return { .hasMapFootprint   = true,
                 .hasWaterfallView  = false,
                 .hasProfileView    = false,
                 .hasFrequencyBands = false,
                 .hasPortStarboard  = false,
                 .hasBottomTrack    = false,
                 .hasTraceSamples   = false,
                 .hasMagSamples     = false };
    }
    return {};
}

} // namespace dolphin::core
