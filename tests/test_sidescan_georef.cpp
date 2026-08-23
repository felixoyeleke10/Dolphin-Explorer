// Regression tests for map swath georeferencing.
//
// Covers:
//   - track-derived heading takes precedence over raw non-zero sensor heading
//   - repeated nav positions keep the last valid track direction instead of
//     spinning swaths around one point
//   - held GPS fixes and bounded missing-nav runs are interpolated by timestamp
//   - port/starboard records from one firing cycle receive one repaired pose
//   - real survey breaks, large jumps, and unbounded gaps are not invented over

#include "core/SidescanPing.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"
#include "ui/features/map/sidescan/SidescanMapLoadParams.h"
#include "ui/features/map/sidescan/SidescanInvalidation.h"
#include "ui/features/map/sidescan/SssContinuity.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "ui/features/map/sidescan/SssGeometryPolicy.h"
#include "ui/features/map/sidescan/SwathRasterizer.h"
#include "ui/features/map/MapLongitude.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <utility>
#include <numbers>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while (false)

namespace {

using namespace dolphin;

void testQualityLoadPlanning()
{
    using ui::MapSonarQuality;
    using ui::detail::qualityLoadPlan;

    auto plan = qualityLoadPlan(MapSonarQuality::High, true);
    CHECK(plan.build_quality == MapSonarQuality::High);
    CHECK(!plan.stage_upgrade);

    plan = qualityLoadPlan(MapSonarQuality::Medium, true);
    CHECK(plan.build_quality == MapSonarQuality::Medium);
    CHECK(!plan.stage_upgrade);

    plan = qualityLoadPlan(MapSonarQuality::Medium, false);
    CHECK(plan.build_quality == MapSonarQuality::Low);
    CHECK(!plan.stage_upgrade);

    plan = qualityLoadPlan(MapSonarQuality::Low, true);
    CHECK(plan.build_quality == MapSonarQuality::Low);
    CHECK(!plan.stage_upgrade);
}

void testSidescanInvalidationContract()
{
    using namespace dolphin::ui;
    const auto plan = coalesceSidescanInvalidations({
        {"line-a", SidescanInvalidation::Appearance},
        {"line-a", SidescanInvalidation::Amplitude},
        {"line-a", SidescanInvalidation::Geometry},
        {"line-a", SidescanInvalidation::SourceData},
        {"line-b", SidescanInvalidation::Appearance},
        {"",       SidescanInvalidation::SourceData}
    });
    CHECK(plan.size() == 2);
    CHECK(plan.at("line-a") == SidescanRefreshAction::Reload);
    CHECK(plan.at("line-b") == SidescanRefreshAction::Recolour);
    CHECK(refreshActionFor(SidescanInvalidation::Amplitude)
          == SidescanRefreshAction::Reraster);
    CHECK(refreshActionFor(SidescanInvalidation::Geometry)
          == SidescanRefreshAction::ProgressiveReraster);
}

core::SidescanPing makePing(double lat_m,
                            double lon_m,
                            float  heading_deg,
                            int64_t timestamp_us)
{
    core::SidescanPing ping;
    ping.timestamp_us       = timestamp_us;
    ping.channel            = core::SidescanChannel::Starboard;
    ping.nav.lat            = lat_m;
    ping.nav.lon            = lon_m;
    ping.nav.heading_deg    = heading_deg;
    ping.nav.valid          = true;
    ping.nav.is_projected   = true;
    ping.nav.spatial_ref    = core::makeUnknownProjectedSpatialRef();
    ping.slant_range_m      = 20.0f;
    ping.samples.push_back({1000u, 20.0f});
    return ping;
}

std::vector<size_t> naturalOrder(size_t count)
{
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    return order;
}

void clearNav(core::SidescanPing& ping)
{
    ping.nav.lat   = 0.0;
    ping.nav.lon   = 0.0;
    ping.nav.valid = false;
    ping.qc_flags |= static_cast<uint8_t>(core::QcFlag::NoNav);
}

void testHeldFixesInterpolateByCycleTimestamp()
{
    std::vector<core::SidescanPing> pings;
    const auto add_cycle = [&](uint32_t ping_number,
                               int64_t timestamp_us,
                               double easting_m) {
        auto port = makePing(200.0, easting_m, 0.0f, timestamp_us);
        port.channel = core::SidescanChannel::Port;
        port.ping_number = ping_number;
        pings.push_back(port);

        auto starboard = makePing(200.0, easting_m, 0.0f, timestamp_us + 100);
        starboard.channel = core::SidescanChannel::Starboard;
        starboard.ping_number = ping_number;
        pings.push_back(starboard);
    };

    // GPS updates only at the first and fourth cycles. The uneven timestamps
    // prove that repair is time-weighted rather than index-weighted.
    add_cycle(1, 1'000'000, 100.0);
    add_cycle(2, 2'500'000, 100.0);
    add_cycle(3, 4'000'000, 100.0);
    add_cycle(4, 5'000'000, 104.0);

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::CourseOverGround;
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), params);

    CHECK(table.size() == 8);
    if (table.size() != 8)
        return;

    const double expected[] = {100.0, 100.0, 101.5, 101.5,
                               103.0, 103.0, 104.0, 104.0};
    for (size_t i = 0; i < table.size(); ++i) {
        CHECK(table[i].valid);
        CHECK(table[i].heading_valid);
        CHECK(std::abs(table[i].lon - expected[i]) < 1e-9);
        CHECK(std::abs(table[i].lat - 200.0) < 1e-9);
        if (i > 0 && i < 6)
            CHECK((table[i].flags & ui::kNavFlagInterpolated) != 0);
    }

    // Both channels from every firing cycle must share exactly one pose.
    for (size_t i = 0; i < table.size(); i += 2) {
        CHECK(table[i].lat == table[i + 1].lat);
        CHECK(table[i].lon == table[i + 1].lon);
    }
}

void testValidHeldFixesAtPointOneHertzGpsAreInterpolated()
{
    std::vector<core::SidescanPing> pings;
    for (uint32_t cycle = 0; cycle <= 10; ++cycle) {
        const double easting = cycle == 10 ? 160.0 : 100.0;
        for (const auto channel : {core::SidescanChannel::Port,
                                   core::SidescanChannel::Starboard}) {
            auto ping = makePing(250.0, easting, 0.0f,
                1'000'000 + static_cast<int64_t>(cycle) * 1'000'000
                + (channel == core::SidescanChannel::Starboard ? 50'000 : 0));
            ping.channel = channel;
            ping.ping_number = 1'000 + cycle;
            pings.push_back(std::move(ping));
        }
    }

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::CourseOverGround;
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), params);
    CHECK(table.size() == pings.size());
    if (table.size() != pings.size())
        return;

    for (size_t cycle = 0; cycle <= 10; ++cycle) {
        const double expected = 100.0 + 6.0 * static_cast<double>(cycle);
        const size_t port = cycle * 2;
        const size_t starboard = port + 1;
        CHECK(std::abs(table[port].lon - expected) < 1e-9);
        CHECK(table[port].lon == table[starboard].lon);
        CHECK(table[port].heading_valid);
        CHECK(table[starboard].heading_valid);
    }
}

void testRepairHardBoundsRejectTenKilometreLineBreak()
{
    std::vector<core::SidescanPing> pings;
    for (uint32_t i = 0; i <= 60; ++i) {
        const double easting = i == 60 ? 10'100.0 : 100.0;
        auto ping = makePing(275.0, easting, 0.0f,
            1'000'000 + static_cast<int64_t>(i) * 10'000'000);
        ping.ping_number = i + 1;
        pings.push_back(std::move(ping));
    }

    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), ui::SssGeorefParams{});
    CHECK(table.size() == pings.size());
    if (table.size() == pings.size()) {
        CHECK(table[30].lon == 100.0);
        CHECK((table[30].flags & ui::kNavFlagInterpolated) == 0);
        CHECK(table.back().lon == 10'100.0);
    }
}

void testBoundedMissingNavRunGetsPositionAndCog()
{
    std::vector<core::SidescanPing> pings;
    pings.push_back(makePing(300.0, 100.0, 0.0f, 1'000'000));

    auto missing_1 = makePing(0.0, 0.0, 0.0f, 2'000'000);
    clearNav(missing_1);
    pings.push_back(missing_1);

    auto missing_2 = makePing(0.0, 0.0, 0.0f, 4'000'000);
    clearNav(missing_2);
    pings.push_back(missing_2);

    pings.push_back(makePing(300.0, 104.0, 0.0f, 5'000'000));

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::CourseOverGround;
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), params);

    CHECK(table.size() == 4);
    if (table.size() != 4)
        return;

    CHECK(table[1].valid);
    CHECK(table[2].valid);
    CHECK(std::abs(table[1].lon - 101.0) < 1e-9);
    CHECK(std::abs(table[2].lon - 103.0) < 1e-9);
    CHECK((table[1].flags & ui::kNavFlagInterpolated) != 0);
    CHECK((table[2].flags & ui::kNavFlagInterpolated) != 0);
    CHECK(table[1].heading_valid);
    CHECK(table[2].heading_valid);
}

void testRepairDoesNotBridgeSurveyBreakOrLargeJump()
{
    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::CourseOverGround;

    // A held fix with its next anchor more than five seconds away is a survey
    // break, not a sparse-GPS interpolation interval.
    {
        std::vector<core::SidescanPing> pings;
        pings.push_back(makePing(400.0, 100.0, 0.0f, 1'000'000));
        pings.push_back(makePing(400.0, 100.0, 0.0f, 2'000'000));
        pings.push_back(makePing(400.0, 104.0, 0.0f, 7'000'001));
        const auto table = ui::buildCorrectedNavTable(
            pings, naturalOrder(pings.size()), params);
        CHECK(table.size() == 3);
        if (table.size() == 3) {
            CHECK(table[1].lon == 100.0);
            CHECK((table[1].flags & ui::kNavFlagInterpolated) == 0);
        }
    }

    // A missing ping between anchors over 50 metres apart remains unresolved.
    {
        std::vector<core::SidescanPing> pings;
        pings.push_back(makePing(500.0, 100.0, 0.0f, 1'000'000));
        auto missing = makePing(0.0, 0.0, 0.0f, 2'000'000);
        clearNav(missing);
        pings.push_back(missing);
        pings.push_back(makePing(500.0, 151.0, 0.0f, 3'000'000));
        const auto table = ui::buildCorrectedNavTable(
            pings, naturalOrder(pings.size()), params);
        CHECK(table.size() == 3);
        if (table.size() == 3) {
            CHECK(!table[1].valid);
            CHECK((table[1].flags & ui::kNavFlagInterpolated) == 0);
        }
    }
}

void testRepairDoesNotExtrapolateUnboundedRuns()
{
    std::vector<core::SidescanPing> pings;
    auto leading = makePing(0.0, 0.0, 0.0f, 1'000'000);
    clearNav(leading);
    pings.push_back(leading);
    pings.push_back(makePing(600.0, 100.0, 0.0f, 2'000'000));
    auto trailing = makePing(0.0, 0.0, 0.0f, 3'000'000);
    clearNav(trailing);
    pings.push_back(trailing);

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::CourseOverGround;
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), params);
    CHECK(table.size() == 3);
    if (table.size() == 3) {
        CHECK(!table[0].valid);
        CHECK(table[1].valid);
        CHECK(!table[2].valid);
    }
}

void testReusedPingNumberDoesNotMergeDifferentCycles()
{
    auto port = makePing(700.0, 100.0, 0.0f, 1'000'000);
    port.channel = core::SidescanChannel::Port;
    port.ping_number = 42;

    auto later_starboard = makePing(0.0, 0.0, 0.0f, 2'000'000);
    later_starboard.channel = core::SidescanChannel::Starboard;
    later_starboard.ping_number = 42; // reused/corrupt number, one second later
    clearNav(later_starboard);

    const std::vector<core::SidescanPing> pings{port, later_starboard};
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), ui::SssGeorefParams{});
    CHECK(table.size() == 2);
    if (table.size() == 2) {
        CHECK(table[0].valid);
        CHECK(!table[1].valid);
        CHECK((table[1].flags & ui::kNavFlagInterpolated) == 0);
    }
}

void testDualChannelPoseNormalizationIsTightAndBounded()
{
    auto small_port = makePing(725.0, 100.0, 0.0f, 1'000'000);
    small_port.channel = core::SidescanChannel::Port;
    small_port.ping_number = 10;
    auto small_starboard = makePing(725.0, 100.8, 0.0f, 1'050'000);
    small_starboard.channel = core::SidescanChannel::Starboard;
    small_starboard.ping_number = 10;

    auto conflict_port = makePing(725.0, 200.0, 0.0f, 2'000'000);
    conflict_port.channel = core::SidescanChannel::Port;
    conflict_port.ping_number = 11;
    auto conflict_starboard = makePing(725.0, 205.0, 0.0f, 2'050'000);
    conflict_starboard.channel = core::SidescanChannel::Starboard;
    conflict_starboard.ping_number = 11;

    const std::vector<core::SidescanPing> pings{
        small_port, small_starboard, conflict_port, conflict_starboard};
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), ui::SssGeorefParams{});
    CHECK(table.size() == 4);
    if (table.size() == 4) {
        CHECK(std::abs(table[0].lon - 100.4) < 1e-9);
        CHECK(table[0].lon == table[1].lon);
        CHECK((table[0].flags & ui::kNavFlagInterpolated) != 0);
        CHECK((table[1].flags & ui::kNavFlagInterpolated) != 0);
        CHECK(table[2].lon == 200.0);
        CHECK(table[3].lon == 205.0);
    }
}

void testDualChannelPingResetAndReuseStartNewHeadingSegments()
{
    const auto run_case = [](uint32_t boundary_number) {
        std::vector<core::SidescanPing> pings;
        const auto add_cycle = [&](uint32_t number,
                                   int64_t timestamp,
                                   double easting,
                                   bool missing_starboard) {
            auto port = makePing(750.0, easting, 0.0f, timestamp);
            port.channel = core::SidescanChannel::Port;
            port.ping_number = number;
            pings.push_back(port);

            auto starboard = makePing(750.0, easting, 0.0f, timestamp + 50'000);
            starboard.channel = core::SidescanChannel::Starboard;
            starboard.ping_number = number;
            if (missing_starboard)
                clearNav(starboard);
            pings.push_back(starboard);
        };

        add_cycle(100, 1'000'000, 100.0, false);
        add_cycle(101, 2'000'000, 100.0, false);
        add_cycle(boundary_number, 3'000'000, 100.0, true);
        add_cycle(boundary_number + 1, 4'000'000, 110.0, false);

        ui::SssGeorefParams params;
        params.heading_source = ui::SssHeadingSource::CourseOverGround;
        return ui::buildCorrectedNavTable(
            pings, naturalOrder(pings.size()), params);
    };

    for (const uint32_t boundary_number : {1u, 101u}) {
        const auto table = run_case(boundary_number);
        CHECK(table.size() == 8);
        if (table.size() != 8)
            continue;

        // The reset/reused firing is still a valid port/starboard pair, so its
        // missing channel inherits the companion pose.
        CHECK(table[4].valid);
        CHECK(table[5].valid);
        CHECK(table[4].lon == table[5].lon);
        CHECK((table[5].flags & ui::kNavFlagInterpolated) != 0);

        // Heading may be backfilled inside the new segment, but never backward
        // across the reset/reuse boundary into the old held-position segment.
        CHECK(!table[0].heading_valid);
        CHECK(!table[3].heading_valid);
        CHECK(table[4].heading_valid);
        CHECK(table[7].heading_valid);
        CHECK(std::abs(table[4].heading_rad - std::numbers::pi * 0.5) < 1e-6);
    }
}

void testGeographicInterpolationUsesShortDatelineArc()
{
    std::vector<core::SidescanPing> pings;
    auto left = makePing(0.001, 179.9999, 0.0f, 1'000'000);
    auto held = makePing(0.001, 179.9999, 0.0f, 2'000'000);
    auto right = makePing(0.001, -179.9999, 0.0f, 3'000'000);
    for (auto* ping : {&left, &held, &right}) {
        ping->nav.is_projected = false;
        ping->nav.spatial_ref  = core::makeWgs84SpatialRef();
        pings.push_back(*ping);
    }

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::CourseOverGround;
    const auto table = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), params);
    CHECK(table.size() == 3);
    if (table.size() == 3) {
        CHECK(table[1].valid);
        CHECK(std::abs(std::abs(table[1].lon) - 180.0) < 1e-6);
        CHECK((table[1].flags & ui::kNavFlagInterpolated) != 0);
        for (const auto& nav : table) {
            CHECK(nav.heading_valid);
            CHECK(std::abs(nav.heading_rad - std::numbers::pi * 0.5) < 1e-6);
        }
    }
}

void testDatelineMosaicUsesOneLocalLongitudeBranch()
{
    CHECK(std::abs(geo::wrapLongitude180(181.0) + 179.0) < 1e-12);
    CHECK(std::abs(geo::wrapLongitude180(-181.0) - 179.0) < 1e-12);
    CHECK(std::abs(geo::wrapLongitude180(180.0) + 180.0) < 1e-12);
    CHECK(std::abs(geo::unwrapLongitudeNear(-179.9, 179.9) - 180.1) < 1e-12);

    const double longitudes[] = {179.9994, 179.9997, -179.9999, -179.9996};
    std::vector<core::SidescanPing> pings;
    for (size_t i = 0; i < std::size(longitudes); ++i) {
        auto ping = makePing(10.0, longitudes[i], 90.0f,
                             1'000'000 + static_cast<int64_t>(i) * 1'000'000);
        ping.nav.is_projected = false;
        ping.nav.spatial_ref = core::makeWgs84SpatialRef();
        ping.nav.sensor_heading_deg = 90.0f;
        ping.ping_number = static_cast<uint32_t>(i + 1);
        pings.push_back(std::move(ping));
    }

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::FishSensor;
    const auto corrected = ui::buildCorrectedNavTable(
        pings, naturalOrder(pings.size()), params);
    CHECK(corrected.size() == pings.size());
    if (corrected.size() == pings.size()) {
        double lon_min = std::numeric_limits<double>::infinity();
        double lon_max = -std::numeric_limits<double>::infinity();
        for (const auto& nav : corrected) {
            CHECK(nav.valid);
            lon_min = std::min(lon_min, nav.lon);
            lon_max = std::max(lon_max, nav.lon);
        }
        CHECK(lon_max - lon_min < 0.002);
    }

    ui::LayerMapData data;
    CHECK(ui::buildSwathNavTrack(pings, data, params) == pings.size());
    ui::buildSwathCoverage(pings, data, params);
    const std::atomic_bool cancelled{false};
    CHECK(ui::buildSwathPreviewImage(
        pings, data, 256, 0, cancelled, params, 0.0, 0));
    CHECK(data.lon_max - data.lon_min < 0.01);
    CHECK(!data.preview_image.isNull());
    CHECK(data.nav_stats.cells_rasterized > 0);
    CHECK(data.nav_stats.stitch_nav_rejects == 0);
    CHECK(data.nav_stats.stitch_time_rejects == 0);
    CHECK(data.nav_stats.stitch_ping_rejects == 0);
    for (const auto& point : data.nav_track)
        if (std::isfinite(point.x()))
            CHECK(point.x() > 179.0 && point.x() < 181.0);
    for (const auto& coverage : data.coverage)
        for (const auto& ribbon : coverage.ribbons)
            for (const auto& point : ribbon)
                CHECK(point.x() > 179.0 && point.x() < 181.0);
}

void testRepeatedPositionUsesTrackHeading()
{
    std::vector<core::SidescanPing> pings;
    // Ping 0: valid non-zero position so the north COG can be computed for
    // pings 1-2, but NoNav flag excludes it from strip creation.
    auto p0 = makePing(0.001, 0.0, 0.0f, 1'000'000);
    p0.qc_flags |= static_cast<uint8_t>(core::QcFlag::NoNav);
    pings.push_back(p0);
    pings.push_back(makePing(10.0, 0.0, 90.0f,  2'000'000));
    pings.push_back(makePing(10.0, 0.0, 180.0f, 3'000'000));

    const auto result = dolphin::ui::georeferenceSidescanPings(pings);

    CHECK(result.strips.size() == 2);
    if (result.strips.size() != 2)
        return;

    for (size_t i = 0; i < result.strips.size(); ++i) {
        const auto& strip = result.strips[i];
        CHECK(strip.channel == core::SidescanChannel::Starboard);
        // Each strip has a synthetic nadir point prepended (index 0) followed by
        // the georeferenced sample(s). Expect at least 2 points.
        CHECK(strip.points.size() >= 2);
        if (strip.points.size() < 2)
            continue;

        // Track moves north (lat increases, lon constant), so starboard should
        // project east (positive lon offset) even when raw sensor heading spins.
        // strip.points[1] is the first real sample; [0] is the nadir point.
        CHECK(strip.points[1].lon > pings[i + 1].nav.lon);
        CHECK(std::abs(strip.points[1].lat - pings[i + 1].nav.lat) < 1e-6);
    }
}

void testRasterizerUsesFourthCornerOfTrapezoid()
{
    constexpr int width = 12;
    constexpr int height = 10;
    std::vector<QRgb> pixels(static_cast<size_t>(width) * height, 0);
    std::vector<uint16_t> amplitudes(static_cast<size_t>(width) * height, 0);

    ui::SwathRasterizer rasterizer;
    rasterizer.buildLut(ui::SonarDisplayParams{}, 0);
    const size_t writes = rasterizer.rasterizeCell(
        pixels.data(), width, height,
        QPointF(1.0, 1.0), QPointF(8.0, 1.0),
        QPointF(2.0, 8.0), QPointF(10.0, 8.0),
        1'000, 2'000, 3'000, 4'000,
        std::numeric_limits<int>::max(), amplitudes.data());

    CHECK(writes > 0);
    // This lower-right wedge exists only because nb=(10,8) is honored. The old
    // parallelogram inverse (based solely on pa/na/pb) left it transparent.
    CHECK(amplitudes[6 * width + 9] != 0);
}

void testRasterizerPreservesSubpixelCellsAndRejectsZeroArea()
{
    constexpr int width = 4;
    constexpr int height = 4;
    std::vector<QRgb> pixels(static_cast<size_t>(width) * height, 0);
    std::vector<uint16_t> amplitudes(static_cast<size_t>(width) * height, 0);

    ui::SwathRasterizer rasterizer;
    rasterizer.buildLut(ui::SonarDisplayParams{}, 0);
    for (int i = 0; i < 5; ++i) {
        const double x0 = 0.1 + static_cast<double>(i) * 0.2;
        const double x1 = x0 + 0.2;
        CHECK(rasterizer.rasterizeCell(
            pixels.data(), width, height,
            QPointF(x0, 1.1), QPointF(x1, 1.1),
            QPointF(x0, 2.1), QPointF(x1, 2.1),
            1'000, 1'100, 1'200, 1'300,
            std::numeric_limits<int>::max(), amplitudes.data()) > 0);
    }
    CHECK(amplitudes[1 * width + 0] != 0);
    CHECK(amplitudes[1 * width + 1] != 0);

    CHECK(rasterizer.rasterizeCell(
        pixels.data(), width, height,
        QPointF(0.0, 0.0), QPointF(1.0, 0.0),
        QPointF(2.0, 0.0), QPointF(3.0, 0.0),
        1'000, 1'000, 1'000, 1'000) == 0);

    std::fill(pixels.begin(), pixels.end(), 0);
    std::fill(amplitudes.begin(), amplitudes.end(), 0);
    CHECK(rasterizer.rasterizeCell(
        pixels.data(), width, height,
        QPointF(1.70, 1.10), QPointF(1.80, 1.10),
        QPointF(1.70, 1.90), QPointF(1.80, 1.90),
        1'000, 1'100, 1'200, 1'300,
        std::numeric_limits<int>::max(), amplitudes.data()) == 1);
    CHECK(amplitudes[1 * width + 1] != 0);
    CHECK(amplitudes[1 * width + 2] == 0);
    CHECK(amplitudes[2 * width + 1] == 0);
}

void testCoverageAndRasterShareNadirPolicy()
{
    std::vector<core::SidescanPing> pings;
    pings.push_back(makePing(200.0, 100.0, 90.0f, 1'000'000));
    pings.push_back(makePing(200.0, 110.0, 90.0f, 2'000'000));
    for (size_t i = 0; i < pings.size(); ++i) {
        pings[i].nav.sensor_heading_deg = 90.0f;
        pings[i].nav.altitude_m = 12.0f;
        pings[i].bottom_pick = {5.0f, 1.0f, 1};
        pings[i].ping_number = static_cast<uint32_t>(i + 1);
    }

    const auto verify = [&](bool close_nadir, bool bake_ranges) {
        auto case_pings = pings;
        if (bake_ranges) {
            // Match SlantRangeNode: sample ranges are ground range after SRC,
            // while ping.slant_range_m remains the original outer slant range.
            for (auto& ping : case_pings) {
                for (auto& sample : ping.samples) {
                    sample.range_m = std::sqrt(
                        sample.range_m * sample.range_m
                        - ping.nav.altitude_m * ping.nav.altitude_m);
                }
                ping.correction_flags |= core::CorrectionFlag::SlantRange;
            }
        }
        ui::SssGeorefParams params;
        params.heading_source = ui::SssHeadingSource::FishSensor;
        params.slant_range_corrected = close_nadir;
        // This case exercises the open QC gap, so opt out of the default
        // "show nadir band" display preference (which sets the gap to zero).
        params.show_nadir = false;

        const auto georef = ui::georeferenceSidescanPings(case_pings, params);
        ui::LayerMapData data;
        ui::buildSwathCoverage(case_pings, data, params);
        CHECK(georef.strips.size() == 2);
        CHECK(data.coverage.size() == 1);
        if (georef.strips.size() != 2 || data.coverage.size() != 1
                || data.coverage[0].ribbons.empty())
            return;

        const auto& raster_inner = georef.strips[0].points.front();
        const QPointF coverage_inner = data.coverage[0].ribbons[0].front();
        const auto& raster_outer = georef.strips[0].points.back();
        const QPointF coverage_outer = data.coverage[0].ribbons[0].back();
        CHECK(std::abs(raster_inner.lon - coverage_inner.x()) < 1e-9);
        CHECK(std::abs(raster_inner.lat - coverage_inner.y()) < 1e-9);
        CHECK(std::abs(raster_outer.lon - coverage_outer.x()) < 1e-9);
        CHECK(std::abs(raster_outer.lat - coverage_outer.y()) < 1e-9);
        const double outer_distance = std::hypot(
            raster_outer.lon - pings[0].nav.lon,
            raster_outer.lat - pings[0].nav.lat);
        const double expected_outer = bake_ranges ? 16.0
            : close_nadir ? std::sqrt(375.0) : 20.0;
        CHECK(std::abs(outer_distance - expected_outer) < 1e-6);
        if (close_nadir) {
            CHECK(std::abs(raster_inner.lon - pings[0].nav.lon) < 1e-9);
            CHECK(std::abs(raster_inner.lat - pings[0].nav.lat) < 1e-9);
        } else {
            CHECK(std::hypot(raster_inner.lon - pings[0].nav.lon,
                             raster_inner.lat - pings[0].nav.lat) > 1.0);
        }
    };

    verify(false, false); // Raw sample geometry with an open water column.
    verify(true, false);  // UI-only close; sample ranges are still raw slant.
    verify(true, true);   // SlantRangeNode-baked ground ranges.

    // Showing the nadir band must not make raw and corrected map geometry
    // identical. Raw samples retain their slant-distance footprint; corrected
    // samples are compressed to ground range.
    {
        ui::SssGeorefParams raw_params;
        raw_params.heading_source = ui::SssHeadingSource::FishSensor;
        raw_params.show_nadir = true;
        raw_params.slant_range_corrected = false;
        auto corrected_params = raw_params;
        corrected_params.slant_range_corrected = true;
        const auto raw = ui::georeferenceSidescanPings(pings, raw_params);
        const auto corrected = ui::georeferenceSidescanPings(pings, corrected_params);
        CHECK(raw.strips.size() == corrected.strips.size());
        if (!raw.strips.empty() && !corrected.strips.empty()) {
            CHECK(raw.strips[0].points.back().ground_range_m
                  > corrected.strips[0].points.back().ground_range_m);
        }
    }

    // Default operator preference (show_nadir = true): even without slant
    // correction the near-nadir seabed band is displayed — the inner edge
    // reaches (close to) the track instead of leaving the QC gap open.
    {
        ui::SssGeorefParams band_params;
        band_params.heading_source = ui::SssHeadingSource::FishSensor;
        CHECK(band_params.show_nadir);   // default must be "band shown"
        const auto georef = ui::georeferenceSidescanPings(pings, band_params);
        CHECK(georef.strips.size() == 2);
        if (georef.strips.size() == 2) {
            const auto& inner = georef.strips[0].points.front();
            CHECK(std::hypot(inner.lon - pings[0].nav.lon,
                             inner.lat - pings[0].nav.lat) < 1.0);
        }
    }

    auto baked_zero = pings.front();
    baked_zero.samples = {{1'000, 0.0f}, {1'500, 0.0f}, {2'000, 16.0f}};
    baked_zero.correction_flags |= core::CorrectionFlag::SlantRange;
    ui::SssGeorefParams baked_params;
    baked_params.heading_source = ui::SssHeadingSource::FishSensor;
    baked_params.slant_range_corrected = true;
    const auto zero_result = ui::georeferenceSidescanPings({baked_zero}, baked_params);
    CHECK(zero_result.strips.size() == 1);
    if (zero_result.strips.size() == 1) {
        CHECK(zero_result.strips[0].points.size() == 2);
        CHECK(!zero_result.strips[0].points.front().renderable);
    }

    // Durable caches can carry authoritative per-ping correction flags even
    // when old/migrated layer metadata is absent. That mismatch must neither
    // reopen the nadir nor restore the bottom-return centerline.
    baked_params.slant_range_corrected = false;
    baked_params.show_nadir = false;
    const auto flagged_result = ui::georeferenceSidescanPings({baked_zero}, baked_params);
    CHECK(flagged_result.strips.size() == 1);
    if (flagged_result.strips.size() == 1) {
        CHECK(flagged_result.strips[0].points.size() == 2);
        CHECK(flagged_result.strips[0].points.front().ground_range_m == 0.0f);
        CHECK(!flagged_result.strips[0].points.front().renderable);
    }

    // A UI/layer request is not the same as an applied correction. With no
    // bottom pick or nav altitude the raw ping must retain normal presentation.
    auto no_altitude = pings.front();
    no_altitude.bottom_pick = {};
    no_altitude.nav.altitude_m = 0.0f;
    no_altitude.correction_flags = 0;
    baked_params.slant_range_corrected = true;
    CHECK(!ui::sssCorrectionPresented(no_altitude, baked_params));
}

void testContinuityLearnsLineCadenceNotSurveyBreaks()
{
    const auto thresholds = ui::ssscontinuity::fromDeltas(
        {1.0, 1'000.0, 1.0, 1'000.0, 1.0},
        {1'000'000.0, 100'000'000.0, 1'000'000.0,
         100'000'000.0, 1'000'000.0},
        {1.0, 1'000.0, 1.0, 1'000.0, 1.0});
    CHECK(thresholds.nav_gap_m < 1'000.0);
    CHECK(thresholds.time_gap_us < 100'000'000);
    CHECK(thresholds.ping_gap < 1'000);
}

void testPreviewAlphaMaskKeepsContinuityAndHonestBreaks()
{
    const auto makeLine = [](bool survey_break, bool rejected_middle) {
        std::vector<core::SidescanPing> pings;
        for (uint32_t i = 0; i < 8; ++i) {
            const bool after_break = survey_break && i >= 4;
            const double lon = after_break
                ? 1'100.0 + 10.0 * static_cast<double>(i - 4)
                : 100.0 + 10.0 * static_cast<double>(i);
            const int64_t timestamp = after_break
                ? 101'000'000 + static_cast<int64_t>(i - 4) * 1'000'000
                : 1'000'000 + static_cast<int64_t>(i) * 1'000'000;
            auto ping = makePing(200.0, lon, 90.0f, timestamp);
            ping.nav.sensor_heading_deg = 90.0f;
            ping.ping_number = after_break ? 1'001 + (i - 4) : i + 1;
            if (rejected_middle && i == 4)
                ping.qc_flags |= static_cast<uint8_t>(core::QcFlag::Rejected);
            pings.push_back(std::move(ping));
        }
        return pings;
    };

    const auto build = [](const std::vector<core::SidescanPing>& pings) {
        ui::SssGeorefParams params;
        params.heading_source = ui::SssHeadingSource::FishSensor;
        ui::LayerMapData data;
        const std::atomic_bool cancelled{false};
        CHECK(ui::buildSwathNavTrack(pings, data, params) > 0);
        CHECK(ui::buildSwathPreviewImage(
            pings, data, 256, 0, cancelled, params, 0.0, 0));
        return data;
    };

    const auto alphaBounds = [](const QImage& image,
                                int& min_x, int& max_x,
                                int& min_y, int& max_y) {
        min_x = image.width(); max_x = -1;
        min_y = image.height(); max_y = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) == 0) continue;
                min_x = std::min(min_x, x); max_x = std::max(max_x, x);
                min_y = std::min(min_y, y); max_y = std::max(max_y, y);
            }
        }
        return max_x >= min_x && max_y >= min_y;
    };
    const auto columnHasAlpha = [](const QImage& image, int x) {
        for (int y = 0; y < image.height(); ++y)
            if (qAlpha(image.pixel(x, y)) != 0) return true;
        return false;
    };
    const auto rowHasAlpha = [](const QImage& image, int y) {
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) != 0) return true;
        return false;
    };
    const auto hasEmptyInteriorColumn = [&](const QImage& image) {
        int min_x = 0, max_x = -1, min_y = 0, max_y = -1;
        if (!alphaBounds(image, min_x, max_x, min_y, max_y)) return false;
        for (int x = min_x + 1; x < max_x; ++x)
            if (!columnHasAlpha(image, x)) return true;
        return false;
    };

    const auto continuous = build(makeLine(false, false));
    CHECK(!continuous.preview_image.isNull());
    if (!continuous.preview_image.isNull()) {
        int min_x = 0, max_x = -1, min_y = 0, max_y = -1;
        CHECK(alphaBounds(
            continuous.preview_image, min_x, max_x, min_y, max_y));
        for (int x = min_x; x <= max_x; ++x)
            CHECK(columnHasAlpha(continuous.preview_image, x));
        for (int y = min_y; y <= max_y; ++y)
            CHECK(rowHasAlpha(continuous.preview_image, y));
    }

    const auto broken = build(makeLine(true, false));
    CHECK(!broken.preview_image.isNull());
    if (!broken.preview_image.isNull())
        CHECK(hasEmptyInteriorColumn(broken.preview_image));

    const auto rejected = build(makeLine(false, true));
    CHECK(!rejected.preview_image.isNull());
    if (!rejected.preview_image.isNull())
        CHECK(hasEmptyInteriorColumn(rejected.preview_image));
}

void testHighQualityTierIsDistinctAndBounded()
{
    const auto medium = ui::detail::paramsForQuality(ui::MapSonarQuality::Medium);
    const auto high = ui::detail::paramsForQuality(ui::MapSonarQuality::High);
    CHECK(high.max_ping_groups > 0);
    CHECK(high.max_ping_entries > 0);
    CHECK(high.max_samples_per_ping > medium.max_samples_per_ping);
    CHECK(high.max_image_dim > medium.max_image_dim);
    CHECK(high.max_ping_entries
          * static_cast<size_t>(high.max_samples_per_ping) <= 4'194'304);
    CHECK(ui::detail::kHighSampleWorkingSetBytes
          <= static_cast<size_t>(160) * 1024 * 1024);

    core::ArtifactIndex index;
    for (uint32_t group = 0; group < 100; ++group) {
        for (uint32_t record = 0; record < 4; ++record) {
            core::ArtifactIndexEntry entry;
            entry.artifact_id = static_cast<uint64_t>(group) * 4 + record;
            entry.type = core::ArtifactType::Sidescan;
            entry.timestamp_us = static_cast<int64_t>(group) * 1'000'000;
            entry.ping_number = group + 1;
            index.entries.push_back(entry);
        }
    }
    core::ArtifactIndexEntry unrelated;
    unrelated.type = core::ArtifactType::Magnetometer;
    index.entries.push_back(unrelated);
    const ui::detail::QualityParams tiny{50, 60, 32, 64, 0.0, 0};
    ui::detail::boundSidescanIndexForMap(index, tiny);
    CHECK(index.byType(core::ArtifactType::Sidescan).size() <= 60);
    CHECK(index.byType(core::ArtifactType::Magnetometer).size() == 1);

    core::ArtifactIndex paired;
    for (uint32_t group = 0; group < 50; ++group) {
        for (uint32_t channel = 0; channel < 2; ++channel) {
            core::ArtifactIndexEntry entry;
            entry.artifact_id = static_cast<uint64_t>(group) * 2 + channel;
            entry.type = core::ArtifactType::Sidescan;
            entry.timestamp_us = static_cast<int64_t>(group) * 1'000'000;
            entry.ping_number = group + 1;
            paired.entries.push_back(entry);
        }
    }
    const ui::detail::QualityParams paired_cap{10, 20, 32, 64, 0.0, 0};
    ui::detail::boundSidescanIndexForMap(paired, paired_cap);
    const auto retained = paired.byType(core::ArtifactType::Sidescan);
    CHECK(retained.size() == 20);
    for (const auto* entry : retained) {
        const auto same_cycle = std::count_if(
            retained.begin(), retained.end(), [&](const auto* candidate) {
                return candidate->ping_number == entry->ping_number;
            });
        CHECK(same_cycle == 2);
    }
}

std::vector<core::SidescanPing> makeRegularlyThinnedPings(bool add_survey_break)
{
    std::vector<core::SidescanPing> pings;
    for (uint32_t i = 0; i < 6; ++i) {
        auto ping = makePing(200.0, 100.0 + 60.0 * i, 90.0f,
                             1'000'000 + static_cast<int64_t>(i) * 6'000'000);
        ping.ping_number = 1 + i * 25;
        ping.nav.sensor_heading_deg = 90.0f;
        pings.push_back(std::move(ping));
    }
    if (add_survey_break) {
        auto ping = makePing(200.0, 10'400.0, 90.0f, 631'000'000);
        ping.ping_number = 10'126;
        ping.nav.sensor_heading_deg = 90.0f;
        pings.push_back(std::move(ping));
    }
    return pings;
}

void testRetainedCadenceStitchesButSurveyBreakDoesNot()
{
    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::FishSensor;
    const std::atomic_bool cancelled{false};

    {
        const auto pings = makeRegularlyThinnedPings(false);
        ui::SssGeorefParams cog_params;
        cog_params.heading_source = ui::SssHeadingSource::CourseOverGround;
        const auto corrected = ui::buildCorrectedNavTable(
            pings, naturalOrder(pings.size()), cog_params);
        CHECK(corrected.size() == pings.size());
        for (const auto& nav : corrected) {
            CHECK(nav.heading_valid);
            CHECK(std::abs(nav.heading_rad - std::numbers::pi * 0.5) < 1e-6);
        }

        ui::LayerMapData data;
        CHECK(ui::buildSwathNavTrack(pings, data, params) == pings.size());
        ui::buildSwathCoverage(pings, data, params);
        CHECK(data.coverage.size() == 1);
        CHECK(data.coverage[0].ribbons.size() == 1);
        CHECK(ui::buildSwathPreviewImage(
            pings, data, 256, 0, cancelled, params, 0.0, 0));
        CHECK(data.nav_stats.stitch_nav_rejects == 0);
        CHECK(data.nav_stats.stitch_time_rejects == 0);
        CHECK(data.nav_stats.stitch_ping_rejects == 0);
        CHECK(data.nav_stats.cells_attempted == 5);
        CHECK(data.nav_stats.cells_rasterized == 5);
        CHECK(!data.preview_image.isNull());
    }

    {
        const auto pings = makeRegularlyThinnedPings(true);
        ui::LayerMapData data;
        CHECK(ui::buildSwathNavTrack(pings, data, params) == pings.size());
        CHECK(std::count_if(data.nav_track.begin(), data.nav_track.end(),
            [](const QPointF& point) {
                return std::isnan(point.x()) && std::isnan(point.y());
            }) == 1);
        ui::buildSwathCoverage(pings, data, params);
        CHECK(data.coverage.size() == 1);
        CHECK(data.coverage[0].ribbons.size() == 1);
        CHECK(ui::buildSwathPreviewImage(
            pings, data, 256, 0, cancelled, params, 0.0, 0));
        CHECK(data.nav_stats.stitch_nav_rejects == 1);
        CHECK(data.nav_stats.cells_attempted == 5);
        CHECK(data.nav_stats.cells_rasterized == 5);
    }
}

void testUnequalStripSampleCountsUseFullSwath()
{
    auto first = makePing(200.0, 100.0, 90.0f, 1'000'000);
    auto second = makePing(200.0, 110.0, 90.0f, 2'000'000);
    first.nav.sensor_heading_deg = second.nav.sensor_heading_deg = 90.0f;
    first.nav.altitude_m = second.nav.altitude_m = 1.0f;
    first.samples = {{1'000, 1.0f}, {2'000, 20.0f}};
    second.samples = {{1'000, 1.0f}, {1'250, 5.0f}, {1'500, 10.0f},
                      {1'750, 15.0f}, {2'000, 20.0f}};
    const std::vector<core::SidescanPing> pings{first, second};

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::FishSensor;
    params.slant_range_corrected = true;
    ui::LayerMapData data;
    CHECK(ui::buildSwathNavTrack(pings, data, params) == 2);
    const std::atomic_bool cancelled{false};
    CHECK(ui::buildSwathPreviewImage(
        pings, data, 128, 0, cancelled, params, 0.0, 0));
    // The geometry-only nadir anchor is not rasterized; the remaining three
    // real seabed cells still retain the full unequal-sample outer swath. The
    // old min-size zip emitted only two and discarded the far-range wedge.
    CHECK(data.nav_stats.cells_attempted == 3);
    CHECK(data.nav_stats.cells_rasterized == 3);

    // Verify the presentation result, not merely the geometry marker: the
    // midpoint of the navigation centerline must remain transparent rather
    // than becoming either a bright or palette-black seabed stripe.
    CHECK(!data.preview_image.isNull());
    if (!data.preview_image.isNull()) {
        const double mid_lon = 0.5 * (first.nav.lon + second.nav.lon);
        const double mid_lat = 0.5 * (first.nav.lat + second.nav.lat);
        const int x = std::clamp(static_cast<int>(
            (mid_lon - data.lon_min) / (data.lon_max - data.lon_min)
                * data.preview_image.width()), 0, data.preview_image.width() - 1);
        const int y = std::clamp(static_cast<int>(
            (data.lat_max - mid_lat) / (data.lat_max - data.lat_min)
                * data.preview_image.height()), 0, data.preview_image.height() - 1);
        CHECK(qAlpha(data.preview_image.pixel(x, y)) == 0);
    }
}

void testXtfProjectedHintWithDegreeCoordsNormalizesToWgs84()
{
    core::SidescanPing ping = makePing(-46.342447, -73.728627, 0.0f, 1'000'000);
    ping.nav.is_projected = true;
    ping.nav.spatial_ref  = core::makeUnknownProjectedSpatialRef("PROJECTED:XTF_NAVUNITS3");

    auto out = dolphin::geo::normalizeSidescanPingsForMap(
        std::vector<core::SidescanPing>{ping},
        core::makeWgs84SpatialRef());

    CHECK(out.size() == 1);
    if (out.size() != 1)
        return;

    CHECK(!out[0].nav.is_projected);
    CHECK(out[0].nav.spatial_ref.id == "EPSG:4326");
    CHECK(std::abs(out[0].nav.lat + 46.342447) < 1e-6);
    CHECK(std::abs(out[0].nav.lon + 73.728627) < 1e-6);
}

// Forward projection used by the status-bar cursor readout (lat/lon → working
// grid). Locks zone selection, argument ordering, and the unsupported fallback.
void testLatLonToProjected()
{
    using dolphin::geo::latLonToProjected;
    using dolphin::geo::latLonToUtm;

    // EPSG:25829 (ETRS89 / UTM 29N): a point inside zone 29. At lon -8 the
    // auto-zone path also resolves to 29, so the forced-zone result must match
    // latLonToUtm exactly, and northing is returned before easting.
    {
        core::SpatialRef ref; ref.id = "EPSG:25829";
        double n = 0.0, e = 0.0;
        CHECK(latLonToProjected(43.0, -8.0, ref, n, e));
        int zone = 0; bool north = false; double eu = 0.0, nu = 0.0;
        CHECK(latLonToUtm(43.0, -8.0, zone, north, eu, nu));
        CHECK(zone == 29);
        CHECK(std::abs(e - eu) < 1e-3);
        CHECK(std::abs(n - nu) < 1e-3);
        CHECK(n > e);                              // northing first, easting second
        CHECK(n > 4'000'000.0 && n < 5'000'000.0);
    }

    // EPSG:32630 (WGS84 / UTM 30N): point inside zone 30.
    {
        core::SpatialRef ref; ref.id = "EPSG:32630";
        double n = 0.0, e = 0.0;
        CHECK(latLonToProjected(51.0, -2.0, ref, n, e));
        int zone = 0; bool north = false; double eu = 0.0, nu = 0.0;
        CHECK(latLonToUtm(51.0, -2.0, zone, north, eu, nu));
        CHECK(zone == 30);
        CHECK(std::abs(e - eu) < 1e-3);
        CHECK(std::abs(n - nu) < 1e-3);
    }

    // The target's own zone is used, NOT one auto-derived from longitude:
    // lon -6 auto-zones to 30, so projecting it into zone 29 vs 30 must give
    // clearly different eastings (different central meridians).
    {
        core::SpatialRef z29; z29.id = "EPSG:25829";
        core::SpatialRef z30; z30.id = "EPSG:32630";
        double n29 = 0, e29 = 0, n30 = 0, e30 = 0;
        CHECK(latLonToProjected(50.0, -6.0, z29, n29, e29));
        CHECK(latLonToProjected(50.0, -6.0, z30, n30, e30));
        CHECK(std::abs(e29 - e30) > 1000.0);
    }

    // Geographic / empty / unsupported targets fall back (return false) so the
    // caller shows lat/lon instead of bogus metres.
    {
        core::SpatialRef geo4326; geo4326.id = "EPSG:4326";
        core::SpatialRef empty;
        double n = 1.0, e = 1.0;
        CHECK(!latLonToProjected(50.0, -2.0, geo4326, n, e));
        CHECK(!latLonToProjected(50.0, -2.0, empty,   n, e));
    }

    // Antimeridian (lon == 180) must not produce an out-of-range UTM zone (61).
    {
        int zone = 0; bool north = false; double e = 0.0, n = 0.0;
        CHECK(latLonToUtm(0.0, 180.0, zone, north, e, n));
        CHECK(zone >= 1 && zone <= 60);
    }
}

void testBeamRaysRetainPhysicalPerPingGeometry()
{
    std::vector<core::SidescanPing> pings;

    auto port = makePing(200.0, 100.0, 90.0f, 1'000'000);
    port.channel = core::SidescanChannel::Port;
    port.ping_number = 1;
    port.nav.sensor_heading_deg = 90.0f;
    port.nav.altitude_m = 12.0f;
    port.bottom_pick = {5.0f, 0.9f, 1};
    pings.push_back(port);

    auto stbd = makePing(210.0, 110.0, 90.0f, 2'000'000);
    stbd.channel = core::SidescanChannel::Starboard;
    stbd.ping_number = 2;
    stbd.nav.sensor_heading_deg = 90.0f;
    stbd.nav.altitude_m = 12.0f;
    stbd.bottom_pick = {5.0f, 0.9f, 1};
    pings.push_back(stbd);

    // A rejected record must create neither coverage nor an overlay ray.
    auto rejected = makePing(220.0, 120.0, 0.0f, 3'000'000);
    rejected.channel = core::SidescanChannel::Port;
    rejected.nav.sensor_heading_deg = 0.0f;
    rejected.qc_flags |= static_cast<uint8_t>(core::QcFlag::Rejected);
    pings.push_back(rejected);

    ui::SssGeorefParams params;
    params.heading_source = ui::SssHeadingSource::FishSensor;
    params.show_nadir = false;

    ui::LayerMapData data;
    ui::buildSwathCoverage(pings, data, params);
    CHECK(data.coverage.empty()); // one usable record per side cannot form ribbons
    CHECK(data.beam_rays.size() == 2);
    if (data.beam_rays.size() != 2)
        return;

    const auto& port_ray = data.beam_rays[0];
    const auto& stbd_ray = data.beam_rays[1];
    CHECK(port_ray.channel == core::SidescanChannel::Port);
    CHECK(stbd_ray.channel == core::SidescanChannel::Starboard);
    CHECK(port_ray.ping_number == 1);
    CHECK(stbd_ray.ping_number == 2);

    // SRC is not applied in this fixture, so the overlay must retain the raw
    // 20 m slant footprint instead of silently using the 5 m bottom pick to
    // present corrected ground range.
    const double expected_range = 20.0;
    CHECK(std::abs(port_ray.origin.x() - 100.0) < 1e-9);
    CHECK(std::abs(port_ray.origin.y() - 200.0) < 1e-9);
    CHECK(std::abs(stbd_ray.origin.x() - 110.0) < 1e-9);
    CHECK(std::abs(stbd_ray.origin.y() - 210.0) < 1e-9);
    CHECK(std::abs(port_ray.outer.x() - 100.0) < 1e-6);
    CHECK(std::abs(port_ray.outer.y() - (200.0 + expected_range)) < 1e-6);
    CHECK(std::abs(stbd_ray.outer.x() - 110.0) < 1e-6);
    CHECK(std::abs(stbd_ray.outer.y() - (210.0 - expected_range)) < 1e-6);

    // Longitude-branch alignment must move ray endpoints with all other layer
    // geometry or the overlay separates from its mosaic at the antimeridian.
    data.is_projected = false;
    data.lon_min = -179.9;
    data.lon_max = -179.8;
    data.beam_rays[0].origin = QPointF(-179.9, 10.0);
    data.beam_rays[0].outer = QPointF(-179.8, 10.0);
    const double shift = ui::maplongitude::alignLayerToReference(data, 180.1);
    CHECK(std::abs(shift - 360.0) < 1e-9);
    CHECK(std::abs(data.beam_rays[0].origin.x() - 180.1) < 1e-9);
    CHECK(std::abs(data.beam_rays[0].outer.x() - 180.2) < 1e-9);
}

} // namespace

int main()
{
    testQualityLoadPlanning();
    testSidescanInvalidationContract();
    testHeldFixesInterpolateByCycleTimestamp();
    testValidHeldFixesAtPointOneHertzGpsAreInterpolated();
    testRepairHardBoundsRejectTenKilometreLineBreak();
    testBoundedMissingNavRunGetsPositionAndCog();
    testRepairDoesNotBridgeSurveyBreakOrLargeJump();
    testRepairDoesNotExtrapolateUnboundedRuns();
    testReusedPingNumberDoesNotMergeDifferentCycles();
    testDualChannelPoseNormalizationIsTightAndBounded();
    testDualChannelPingResetAndReuseStartNewHeadingSegments();
    testGeographicInterpolationUsesShortDatelineArc();
    testDatelineMosaicUsesOneLocalLongitudeBranch();
    testRepeatedPositionUsesTrackHeading();
    testRasterizerUsesFourthCornerOfTrapezoid();
    testRasterizerPreservesSubpixelCellsAndRejectsZeroArea();
    testCoverageAndRasterShareNadirPolicy();
    testContinuityLearnsLineCadenceNotSurveyBreaks();
    testPreviewAlphaMaskKeepsContinuityAndHonestBreaks();
    testHighQualityTierIsDistinctAndBounded();
    testRetainedCadenceStitchesButSurveyBreakDoesNot();
    testUnequalStripSampleCountsUseFullSwath();
    testXtfProjectedHintWithDegreeCoordsNormalizesToWgs84();
    testLatLonToProjected();
    testBeamRaysRetainPhysicalPerPingGeometry();

    if (g_fail != 0) {
        std::fprintf(stderr, "\n%d checks passed, %d failed\n", g_pass, g_fail);
        return 1;
    }

    std::printf("%d checks passed\n", g_pass);
    return 0;
}
