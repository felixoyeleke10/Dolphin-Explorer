// test_nav_corrections.cpp — unit tests for WaterfallView::runNavCorrections.
// Covers attitude offsets and cross-modality smoothing behavior, including
// longitude wrapping and exact window-size semantics.
#include "ui/features/waterfall/WaterfallView.h"
#include "app/display/NavCorrection.h"
#include "app/display/NavProcessingParams.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
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

static void test_heading_offset_updates_sources_and_wraps()
{
    NavProcessingParams params;
    params.heading_offset_deg = 5.f;

    auto populated = makePing(358.f, 0.f, 0.f);
    populated.nav.sensor_heading_deg = 359.f;
    populated.nav.ship_heading_deg = 1.f;
    auto missing = makePing(0.f, 0.f, 0.f);

    const auto out = WaterfallView::runNavCorrections(
        {populated, missing}, params);
    assert(out[0].nav.heading_deg == 3.f);
    assert(out[0].nav.sensor_heading_deg == 4.f);
    assert(out[0].nav.ship_heading_deg == 6.f);
    assert(out[1].nav.heading_deg == 0.f);
    assert(out[1].nav.sensor_heading_deg == 0.f);
    assert(out[1].nav.ship_heading_deg == 0.f);
}

static void test_smoothing_window_is_total_length()
{
    std::vector<core::SubBottomTrace> traces(5);
    for (size_t i = 0; i < traces.size(); ++i) {
        traces[i].nav.valid = true;
        traces[i].nav.lat = i == 2 ? 10.0 : 0.0;
        traces[i].nav.lon = 1.0;
        traces[i].timestamp_us = static_cast<int64_t>(i) * 1'000'000;
    }

    NavProcessingParams params;
    params.smooth_enabled = true;
    params.smooth_window = 3;
    dolphin::ui::applySbpNavCorrections(traces, params);

    assert(std::abs(traces[2].nav.lat - 10.0 / 3.0) < 1e-9);

    for (auto& trace : traces) trace.nav.lat = 0.0;
    traces[2].nav.lat = 10.0;
    params.smooth_window = 2;
    dolphin::ui::applySbpNavCorrections(traces, params);
    assert(std::abs(traces[2].nav.lat - 5.0) < 1e-9);
}

static void test_smoothing_preserves_antimeridian_branch()
{
    NavProcessingParams params;
    params.smooth_enabled = true;
    params.smooth_window = 3;

    std::vector<core::SidescanPing> pings(3);
    const double longitudes[] = {179.0, -179.0, 179.0};
    for (size_t i = 0; i < pings.size(); ++i) {
        pings[i].nav.valid = true;
        pings[i].nav.lat = 45.0;
        pings[i].nav.lon = longitudes[i];
    }
    const auto smoothed = WaterfallView::runNavCorrections(pings, params);
    assert(std::abs(smoothed[1].nav.lon) > 170.0);

    std::vector<core::SubBottomTrace> traces(3);
    for (size_t i = 0; i < traces.size(); ++i) {
        traces[i].nav.valid = true;
        traces[i].nav.lat = 45.0;
        traces[i].nav.lon = longitudes[i];
        traces[i].timestamp_us = static_cast<int64_t>(i) * 1'000'000;
    }
    dolphin::ui::applySbpNavCorrections(traces, params);
    assert(std::abs(traces[1].nav.lon) > 170.0);
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
    test_heading_offset_updates_sources_and_wraps();
    test_pitch_roll_offsets();
    test_empty_pings();
    test_multiple_offsets_combined();
    test_smoothing_window_is_total_length();
    test_smoothing_preserves_antimeridian_branch();
    return 0;
}
