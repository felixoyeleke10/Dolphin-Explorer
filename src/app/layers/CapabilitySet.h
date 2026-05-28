#pragma once
#include "app/layers/LayerUtils.h"

namespace dolphin::app {

// Per-modality feature capabilities. Drives which UI panels and toolbar
// buttons are enabled for the currently selected layer.
struct CapabilitySet {
    bool has_waterfall   = false;  // sidescan waterfall viewer
    bool has_subbottom   = false;  // sub-bottom profile viewer
    bool has_map         = false;  // 2D / 3D map tile display
    bool has_nav_track   = false;  // navigation track overlay
    bool has_contacts    = false;  // contact pick and list
    bool has_metadata    = false;  // metadata window
    bool has_processing  = false;  // node-graph processing
    bool has_corrections = false;  // nav / heading corrections
};

// Returns the capability set for a given modality.
// Used by MainWindow::updateControlsForModality() to enable/disable UI.
inline CapabilitySet capabilitiesFor(Modality m) noexcept
{
    switch (m) {
    case Modality::Sidescan:
        return { .has_waterfall   = true,
                 .has_map         = true,
                 .has_nav_track   = true,
                 .has_contacts    = true,
                 .has_metadata    = true,
                 .has_processing  = true,
                 .has_corrections = true };

    case Modality::SubBottom:
        return { .has_subbottom   = true,
                 .has_map         = true,
                 .has_nav_track   = true,
                 .has_metadata    = true };

    case Modality::Magnetometer:
        return { .has_map         = true,
                 .has_nav_track   = true,
                 .has_metadata    = true };

    case Modality::Multibeam:
        return { .has_map         = true,
                 .has_nav_track   = true,
                 .has_metadata    = true };

    case Modality::Raster:
        return { .has_map         = true };

    case Modality::Mixed:
        return { .has_waterfall   = true,
                 .has_subbottom   = true,
                 .has_map         = true,
                 .has_nav_track   = true,
                 .has_contacts    = true,
                 .has_metadata    = true,
                 .has_processing  = true,
                 .has_corrections = true };

    case Modality::Unknown:
    default:
        return {};
    }
}

} // namespace dolphin::app
