#pragma once

#include "ui/features/map/sidescan/SssGeorefParams.h"

namespace dolphin::ui::sssnavsmoothing {

void apply(std::vector<CorrectedSssNav>& table,
           const std::vector<core::SidescanPing>& pings,
           const std::vector<size_t>& order,
           SssNavSmoothingMode mode,
           int window);

} // namespace dolphin::ui::sssnavsmoothing
