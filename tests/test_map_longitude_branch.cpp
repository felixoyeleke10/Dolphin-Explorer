#include "ui/features/map/MapLongitude.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while (false)

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-9;
}

void testWrapAndUnwrap()
{
    using namespace dolphin::ui::maplongitude;
    CHECK(near(canonical(180.0), -180.0));
    CHECK(near(canonical(181.0), -179.0));
    CHECK(near(canonical(-181.0), 179.0));
    CHECK(near(unwrapNear(-179.9, 179.9), 180.1));
    CHECK(near(unwrapNear(179.9, -179.9), -180.1));

    const Interval crossing = shortGeographicInterval(-179.9, 179.9);
    CHECK(near(crossing.min, 179.9));
    CHECK(near(crossing.max, 180.1));
}

void testWholeLayerMovesTogether()
{
    using namespace dolphin::ui;

    LayerMapData data;
    data.lon_min = -180.0;
    data.lon_max = -179.8;
    data.lat_min = 10.0;
    data.lat_max = 10.1;
    data.nav_track = {
        {-179.95, 10.0},
        {std::numeric_limits<double>::quiet_NaN(),
         std::numeric_limits<double>::quiet_NaN()},
        {-179.85, 10.1},
    };
    SwathCoverage coverage;
    coverage.ribbons.push_back({
        {-179.96, 10.0}, {-179.84, 10.1}, {-179.82, 10.1}, {-179.98, 10.0}});
    data.coverage.push_back(std::move(coverage));
    data.nav_stats.nav_lon_min   = -180.0;
    data.nav_stats.nav_lon_max   = -179.8;
    data.nav_stats.strip_lon_min = -179.96;
    data.nav_stats.strip_lon_max = -179.84;
    data.track_stats.has_endpoints = true;
    data.track_stats.first_lon = -179.95;
    data.track_stats.last_lon  = -179.85;

    const double delta = maplongitude::alignLayerToReference(data, 179.9);
    CHECK(near(delta, 360.0));
    CHECK(near(data.lon_min, 180.0));
    CHECK(near(data.lon_max, 180.2));
    CHECK(near(data.nav_track.front().x(), 180.05));
    CHECK(std::isnan(data.nav_track[1].x()));
    CHECK(near(data.coverage.front().ribbons.front().front().x(), 180.04));
    CHECK(near(data.nav_stats.nav_lon_min, 180.0));
    CHECK(near(data.nav_stats.strip_lon_max, 180.16));
    CHECK(near(data.track_stats.first_lon, 180.05));
    CHECK(near(data.track_stats.last_lon, 180.15));
}

void testOppositeCanonicalLayerCentresCombineLocally()
{
    using namespace dolphin::ui;

    LayerMapData east;
    east.lon_min = 179.8;
    east.lon_max = 180.0;
    east.lat_min = east.lat_max = 10.0;

    LayerMapData west;
    west.lon_min = -180.0;
    west.lon_max = -179.8;
    west.lat_min = west.lat_max = 10.0;

    maplongitude::alignLayerToReference(
        west, maplongitude::boundsCenter(east));
    const double combined_min = std::min(east.lon_min, west.lon_min);
    const double combined_max = std::max(east.lon_max, west.lon_max);
    CHECK(combined_max - combined_min < 0.5);
    CHECK(combined_min > 179.0);
    CHECK(combined_max < 181.0);
}

} // namespace

int main()
{
    testWrapAndUnwrap();
    testWholeLayerMovesTogether();
    testOppositeCanonicalLayerCentresCombineLocally();

    std::printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
