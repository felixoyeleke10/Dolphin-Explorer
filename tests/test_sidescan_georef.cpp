// Regression tests for map swath georeferencing.
//
// Covers:
//   - track-derived heading takes precedence over raw non-zero sensor heading
//   - repeated nav positions keep the last valid track direction instead of
//     spinning swaths around one point

#include "core/SidescanPing.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"
#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"

#include <cmath>
#include <cstdio>
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

} // namespace

int main()
{
    testRepeatedPositionUsesTrackHeading();
    testXtfProjectedHintWithDegreeCoordsNormalizesToWgs84();

    if (g_fail != 0) {
        std::fprintf(stderr, "\n%d checks passed, %d failed\n", g_pass, g_fail);
        return 1;
    }

    std::printf("%d checks passed\n", g_pass);
    return 0;
}
