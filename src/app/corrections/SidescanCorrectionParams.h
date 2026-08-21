#pragma once
#include "pipeline/SidescanRadiometryAlgorithms.h"

// App-layer correction parameter structs for sidescan amplitude processing.
// These are pure data with no Qt/UI dependency; WaterfallParams.h forwards
// them into namespace dolphin::ui via using aliases.

namespace dolphin::app {

// -- AGC ----------------------------------------------------------------------

using AgcMode = dolphin::pipeline::radiometry::AgcMode;
using AgcSmoothingType = dolphin::pipeline::radiometry::AgcSmoothingType;
using AgcParams = dolphin::pipeline::radiometry::AgcSettings;

// -- TVG ----------------------------------------------------------------------

using TvgParams = dolphin::pipeline::radiometry::TvgSettings;

// -- ARC ----------------------------------------------------------------------

using ArcParams = dolphin::pipeline::radiometry::ArcSettings;

// -- Composite ----------------------------------------------------------------

struct SidescanCorrectionParams {
    TvgParams tvg;
    ArcParams arc;
    AgcParams agc;
};

} // namespace dolphin::app
