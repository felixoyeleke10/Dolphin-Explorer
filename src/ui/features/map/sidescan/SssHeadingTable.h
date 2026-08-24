#pragma once

#include "ui/features/map/sidescan/SssGeorefParams.h"

namespace dolphin::ui::sssheading {

std::vector<double> buildTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const SssGeorefParams& params,
    const std::vector<CorrectedSssNav>& positions,
    HeadingStats* out_stats = nullptr);

} // namespace dolphin::ui::sssheading
