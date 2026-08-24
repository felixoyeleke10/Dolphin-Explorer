#pragma once
#include <string>
#include "app/display/NavProcessingParams.h"
#include "app/display/WaterfallParams.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"

namespace dolphin::ui::rastercache::detail {

unsigned long long makeRasterFingerprint(
    const NavProcessingParams& nav,
    const SssGeorefParams& georef,
    const std::string& display_crs_id,
    const WaterfallParams& sidescan);

} // namespace dolphin::ui::rastercache::detail
