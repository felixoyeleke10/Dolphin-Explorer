#include "ui/features/map/sidescan/SssNavSmoothing.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace core = dolphin::core;
namespace smoothing = dolphin::ui::sssnavsmoothing;
using dolphin::ui::CorrectedSssNav;
using dolphin::ui::SssNavSmoothingMode;

namespace {

CorrectedSssNav point(double x)
{
    CorrectedSssNav value;
    value.valid = true;
    value.is_projected = true;
    value.lon = x;
    return value;
}

std::vector<core::SidescanPing> pings(size_t count)
{
    std::vector<core::SidescanPing> result(count);
    for (size_t i = 0; i < count; ++i) {
        result[i].channel = core::SidescanChannel::Port;
        result[i].ping_number = static_cast<uint32_t>(i + 1);
        result[i].timestamp_us = static_cast<int64_t>(i + 1) * 1'000'000;
    }
    return result;
}

} // namespace

int main()
{
    const auto samples = pings(3);
    const std::vector<size_t> order{0, 1, 2};
    {
        std::vector<CorrectedSssNav> table{point(0.0), point(2.0), point(10.0)};
        smoothing::apply(table, samples, order,
                         SssNavSmoothingMode::MovingAverage, 3);
        assert(std::abs(table[1].lon - 4.0) < 1e-9);
    }
    {
        std::vector<CorrectedSssNav> table{point(0.0), point(2.0), point(10.0)};
        smoothing::apply(table, samples, order,
                         SssNavSmoothingMode::Median, 3);
        assert(std::abs(table[1].lon - 2.0) < 1e-9);
    }
    {
        std::vector<CorrectedSssNav> table{point(0.0), point(1000.0), point(10.0)};
        smoothing::apply(table, samples, order,
                         SssNavSmoothingMode::SpikeRejection, 3);
        assert(std::abs(table[1].lon - 5.0) < 1e-9);
        assert((table[1].flags & dolphin::ui::kNavFlagInterpolated) != 0);
    }
    std::cout << "SssNavSmoothing checks passed\n";
    return 0;
}
