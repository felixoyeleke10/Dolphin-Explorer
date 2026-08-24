#pragma once

#include "ui/features/map/sidescan/SssGeorefParams.h"

namespace dolphin::ui::sssnavrepair {

void repairBoundedRuns(
    std::vector<CorrectedSssNav>& table,
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order);

} // namespace dolphin::ui::sssnavrepair
