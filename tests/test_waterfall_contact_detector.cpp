#include "ui/features/waterfall/processing/WaterfallContactDetector.h"

#include <cassert>
#include <iostream>

using namespace dolphin::ui;

namespace {

PingRow targetRow(float range_m = 50.f)
{
    PingRow row;
    row.slant_range_m = range_m;
    row.port.assign(32, 1000);
    row.stbd.assign(32, 1000);
    row.port[16] = 3000;
    for (int i = 18; i <= 23; ++i) row.port[static_cast<size_t>(i)] = 200;
    return row;
}

} // namespace

int main()
{
    {
        const auto found = WaterfallContactDetector::detect(
            {targetRow()}, {}, ContactClass::Debris, 1);
        assert(found.size() == 1);
        assert(found[0].row_idx == 0);
        assert(found[0].ch == dolphin::core::SidescanChannel::Port);
        assert(found[0].classification == ContactClass::Debris);
        assert(found[0].range_m > 0.f);
    }
    {
        const auto first = WaterfallContactDetector::detect(
            {targetRow()}, {}, ContactClass::Unknown, 1);
        const auto duplicate = WaterfallContactDetector::detect(
            {targetRow()}, first, ContactClass::Unknown, 1);
        assert(duplicate.empty());
    }
    {
        std::vector<PingRow> rows(30);
        for (auto& row : rows) row = targetRow();
        const auto capped = WaterfallContactDetector::detect(
            rows, {}, ContactClass::Anomaly, 2, 3);
        assert(capped.size() == 3);
        assert(capped[1].row_idx - capped[0].row_idx >= 5);
    }
    std::cout << "WaterfallContactDetector checks passed\n";
    return 0;
}
