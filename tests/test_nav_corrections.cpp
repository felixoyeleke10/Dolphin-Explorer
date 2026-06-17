// test_nav_corrections.cpp — unit tests for WaterfallView::runNavCorrections.
// Covers the attitude-offset paths (heading/pitch/roll), which need no pipeline
// nodes and no Qt.  Layback and smoothing are node-dependent and tested
// implicitly via integration.
#include "ui/features/waterfall/WaterfallView.h"
#include "app/display/NavProcessingParams.h"
#include "core/SidescanPing.h"
#include <cassert>
#include <cmath>
#include <vector>

using dolphin::ui::NavProcessingParams;
using dolphin::ui::WaterfallView;
namespace core = dolphin::core;

static core::SidescanPing makePing(float heading, float pitch, float roll)
{
    core::SidescanPing p;
    p.nav.heading_deg = heading;
    p.nav.pitch_deg   = pitch;
    p.nav.roll_deg    = roll;
    return p;
}

static void test_identity()
{
    // No corrections → pings unchanged.
    NavProcessingParams params;  // all defaults: offsets = 0, layback/smooth disabled
    std::vector<core::SidescanPing> pings = { makePing(90.f, 1.f, 2.f) };
    auto out = WaterfallView::runNavCorrections(pings, params);
    assert(out.size() == 1);
    assert(out[0].nav.heading_deg == 90.f);
    assert(out[0].nav.pitch_deg   == 1.f);
    assert(out[0].nav.roll_deg    == 2.f);
}

static void test_heading_offset()
{
    NavProcessingParams params;
    params.heading_offset_deg = 5.f;
    std::vector<core::SidescanPing> pings = { makePing(10.f, 0.f, 0.f),
                                               makePing(20.f, 0.f, 0.f) };
    auto out = WaterfallView::runNavCorrections(pings, params);
    assert(out.size() == 2);
    assert(out[0].nav.heading_deg == 15.f);
    assert(out[1].nav.heading_deg == 25.f);
}

static void test_pitch_roll_offsets()
{
    NavProcessingParams params;
    params.pitch_offset_deg = -2.f;
    params.roll_offset_deg  =  3.f;
    std::vector<core::SidescanPing> pings = { makePing(0.f, 5.f, 1.f) };
    auto out = WaterfallView::runNavCorrections(pings, params);
    assert(out.size() == 1);
    assert(out[0].nav.heading_deg == 0.f);
    assert(out[0].nav.pitch_deg   == 3.f);
    assert(out[0].nav.roll_deg    == 4.f);
}

static void test_empty_pings()
{
    NavProcessingParams params;
    params.heading_offset_deg = 10.f;
    std::vector<core::SidescanPing> pings;
    auto out = WaterfallView::runNavCorrections(pings, params);
    assert(out.empty());
}

static void test_multiple_offsets_combined()
{
    NavProcessingParams params;
    params.heading_offset_deg = 1.f;
    params.pitch_offset_deg   = 2.f;
    params.roll_offset_deg    = 3.f;
    std::vector<core::SidescanPing> pings = { makePing(100.f, 0.f, -5.f) };
    auto out = WaterfallView::runNavCorrections(pings, params);
    assert(out.size() == 1);
    assert(out[0].nav.heading_deg == 101.f);
    assert(out[0].nav.pitch_deg   ==   2.f);
    assert(out[0].nav.roll_deg    ==  -2.f);
}

int main()
{
    test_identity();
    test_heading_offset();
    test_pitch_roll_offsets();
    test_empty_pings();
    test_multiple_offsets_combined();
    return 0;
}
