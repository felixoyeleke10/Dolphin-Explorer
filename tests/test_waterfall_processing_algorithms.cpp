#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using dolphin::ui::PingRow;
using dolphin::ui::WaterfallParams;
namespace core = dolphin::core;
namespace wf = dolphin::ui::detail;

namespace {

core::SidescanPing makePing(float blanking_m, float slant_range_m, int samples, uint16_t amp)
{
    core::SidescanPing ping;
    ping.blanking_m = blanking_m;
    ping.slant_range_m = slant_range_m;
    ping.samples.resize(static_cast<size_t>(samples));
    for (auto& sample : ping.samples)
        sample.amplitude = amp;
    return ping;
}

bool near(float a, float b, float eps = 2.f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main()
{
    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.f;
        params.tvg.absorption = 0.f;

        std::vector<core::SidescanPing> pings = {makePing(1.f, 10.f, 10, 1000)};
        wf::applyTvg(pings, params);

        assert(pings[0].samples.front().amplitude == 1000);
        assert(near(static_cast<float>(pings[0].samples.back().amplitude), 10000.f));
    }

    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.f;
        params.tvg.absorption = 0.f;

        std::vector<core::SidescanPing> pings = {makePing(0.f, 10.f, 10, 1000)};
        wf::applyTvg(pings, params);

        assert(pings[0].samples.front().amplitude == 1000);
        assert(near(static_cast<float>(pings[0].samples.back().amplitude), 10000.f));
    }

    {
        WaterfallParams params;
        params.tvg.enabled = true;
        params.tvg.spreading = 20.f;
        params.tvg.absorption = 1.f;

        std::vector<core::SidescanPing> pings = {makePing(12.f, 10.f, 10, 1000)};
        wf::applyTvg(pings, params);

        for (const auto& sample : pings[0].samples)
            assert(sample.amplitude == 1000);
    }

    {
        WaterfallParams params;
        params.arn.enabled = true;
        params.arn.strength = 1.f;
        params.arn.gain_cap_db = 20.f;
        params.arn.column_smooth = 0;

        std::vector<PingRow> rows(5);
        for (auto& row : rows) {
            row.port = {10000, 20000, 40000};
            row.stbd = {10000, 20000, 40000};
        }

        wf::applyArn(rows, params);

        for (const auto& row : rows) {
            assert(near(static_cast<float>(row.port[0]), 32768.f));
            assert(near(static_cast<float>(row.port[1]), 32768.f));
            assert(near(static_cast<float>(row.port[2]), 32768.f));
            assert(near(static_cast<float>(row.stbd[0]), 32768.f));
            assert(near(static_cast<float>(row.stbd[1]), 32768.f));
            assert(near(static_cast<float>(row.stbd[2]), 32768.f));
        }
    }

    {
        WaterfallParams params;
        params.arn.enabled = true;
        params.arn.strength = 1.f;
        params.arn.gain_cap_db = 20.f;
        params.arn.column_smooth = 1;

        std::vector<PingRow> rows(5);
        for (auto& row : rows) {
            row.port = {10000, 0, 10000};
            row.stbd = {10000, 0, 10000};
        }

        wf::applyArn(rows, params);

        for (const auto& row : rows) {
            assert(near(static_cast<float>(row.port[0]), 32768.f));
            assert(row.port[1] == 0);
            assert(near(static_cast<float>(row.port[2]), 32768.f));
            assert(near(static_cast<float>(row.stbd[0]), 32768.f));
            assert(row.stbd[1] == 0);
            assert(near(static_cast<float>(row.stbd[2]), 32768.f));
        }
    }

    return 0;
}
