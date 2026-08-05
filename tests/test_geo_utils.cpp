#include "geo/CoordinateTransformService.h"
#include "geo/GeoUtils.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void expectNear(double actual, double expected, double tolerance)
{
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "expected " << expected << " +/- " << tolerance
                  << ", got " << actual << '\n';
        std::abort();
    }
}

dolphin::core::NavPoint geographic(double latitude, double longitude)
{
    dolphin::core::NavPoint point;
    point.valid = true;
    point.lat = latitude;
    point.lon = longitude;
    point.spatial_ref = dolphin::core::makeWgs84SpatialRef();
    return point;
}

} // namespace

int main()
{
    using namespace dolphin;

    // GeographicLib reference example: Wellington to Salamanca on WGS84.
    expectNear(geo::haversineMetres(-41.32, 174.81, 40.96, -5.50),
               19959679.26735382, 1e-6);

    // The shortest ellipsoidal path crosses the date line, not the whole map.
    expectNear(geo::haversineMetres(0.0, 179.9, 0.0, -179.9),
               22263.89815865, 1e-5);

    int zone = 0;
    bool north = false;
    double easting = 0.0;
    double northing = 0.0;
    assert(geo::latLonToUtm(0.0, 3.0, zone, north, easting, northing));
    assert(zone == 31 && north);
    expectNear(easting, 500000.0, 1e-6);
    expectNear(northing, 0.0, 1e-6);

    // Beyond UTM's valid latitude range the projection is severely distorted;
    // this codebase has no polar-stereographic/UPS fallback, so it must fail
    // cleanly rather than silently returning a badly warped point.
    assert(!geo::latLonToUtm(89.9999, 45.0, zone, north, easting, northing));
    assert(!geo::latLonToUtm(-85.0, 45.0, zone, north, easting, northing));

    // Exercise a non-UTM projected CRS; the old whitelist rejected this.
    const auto web_mercator = geo::spatialRefFromId("EPSG:3857");
    assert(core::spatialRefIsProjected(web_mercator));
    assert(geo::latLonToProjected(0.0, 180.0, web_mercator, northing, easting));
    expectNear(easting, 20037508.342789244, 1e-6);
    expectNear(northing, 0.0, 1e-9);

    core::NavPoint projected;
    projected.valid = true;
    projected.lon = easting;
    projected.lat = northing;
    projected.is_projected = true;
    projected.spatial_ref = web_mercator;
    core::NavPoint round_trip;
    assert(geo::normalizeNavForMap(projected, core::makeWgs84SpatialRef(), round_trip));
    expectNear(round_trip.lon, 180.0, 1e-10);
    expectNear(round_trip.lat, 0.0, 1e-10);

    // Direct geodesic remains defined near the pole where metres/degree fails.
    const auto polar = geographic(89.9999, 45.0);
    double moved_lon = 0.0;
    double moved_lat = 0.0;
    assert(geo::offsetNavByGroundMetres(polar, 1000.0, 0.0, moved_lon, moved_lat));
    expectNear(geo::haversineMetres(polar.lat, polar.lon, moved_lat, moved_lon),
               1000.0, 1e-7);

    const auto origin = geographic(0.0, 0.0);
    const auto east = geographic(0.0, 1.0);
    expectNear(geo::headingFromNavDeltaRad(origin, east), std::acos(-1.0) / 2.0, 1e-14);

    geo::CoordinateTransformService transforms;
    assert(transforms.canTransform(core::makeWgs84SpatialRef(), web_mercator));
    assert(transforms.canTransform(geo::spatialRefFromId("EPSG:23031"),
                                   core::makeWgs84SpatialRef()));
    assert(!transforms.canTransform(geo::spatialRefFromId("LOCAL:UNKNOWN"),
                                    core::makeWgs84SpatialRef()));

    std::cout << "GeoUtils scientific reference tests passed\n";
    return 0;
}
