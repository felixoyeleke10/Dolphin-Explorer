#include "ui/features/map/sidescan/SssNavRepair.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace core = dolphin::core;
namespace repair = dolphin::ui::sssnavrepair;
using dolphin::ui::CorrectedSssNav;

namespace {

core::SidescanPing ping(uint32_t number, int64_t timestamp,
                        core::SidescanChannel channel = core::SidescanChannel::Port)
{
    core::SidescanPing value;
    value.ping_number = number;
    value.timestamp_us = timestamp;
    value.channel = channel;
    return value;
}

CorrectedSssNav position(double x)
{
    CorrectedSssNav value;
    value.valid = true;
    value.is_projected = true;
    value.lat = 0.0;
    value.lon = x;
    return value;
}

} // namespace

int main()
{
    {
        std::vector<core::SidescanPing> pings{
            ping(1, 1'000'000), ping(2, 2'000'000), ping(3, 3'000'000)};
        std::vector<size_t> order{0, 1, 2};
        std::vector<CorrectedSssNav> table{position(100.0), {}, position(104.0)};
        repair::repairBoundedRuns(table, pings, order);
        assert(table[1].valid);
        assert(std::abs(table[1].lon - 102.0) < 1e-9);
        assert((table[1].flags & dolphin::ui::kNavFlagInterpolated) != 0);
    }
    {
        std::vector<core::SidescanPing> pings{
            ping(10, 1'000'000), ping(1, 2'000'000), ping(2, 3'000'000)};
        std::vector<size_t> order{0, 1, 2};
        std::vector<CorrectedSssNav> table{position(100.0), {}, position(102.0)};
        repair::repairBoundedRuns(table, pings, order);
        assert(!table[1].valid);
    }
    {
        std::vector<core::SidescanPing> pings{
            ping(7, 1'000'000, core::SidescanChannel::Port),
            ping(7, 1'000'050, core::SidescanChannel::Starboard)};
        std::vector<size_t> order{0, 1};
        std::vector<CorrectedSssNav> table{position(250.0), {}};
        repair::repairBoundedRuns(table, pings, order);
        assert(table[1].valid);
        assert(table[1].lon == 250.0);
    }
    std::cout << "SssNavRepair checks passed\n";
    return 0;
}
