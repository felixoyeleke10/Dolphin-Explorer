#include "geo/GeoUtils.h"

#include <geodesic.h>
#include <proj.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace dolphin::geo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
// Standard UTM validity range. Beyond these latitudes UTM projections become
// severely distorted; this codebase has no polar-stereographic/UPS support,
// so fail cleanly here rather than silently returning a badly warped point.
constexpr double kUtmMinLat = -80.0;
constexpr double kUtmMaxLat = 84.0;

using ContextPtr = std::unique_ptr<PJ_CONTEXT, decltype(&proj_context_destroy)>;
using ProjPtr = std::unique_ptr<PJ, decltype(&proj_destroy)>;

ContextPtr makeProjContext()
{
    ContextPtr context(proj_context_create(), proj_context_destroy);
#ifdef DOLPHIN_PROJ_DATA_DIR
    if (context) {
        const char* paths[] = {DOLPHIN_PROJ_DATA_DIR};
        proj_context_set_search_paths(context.get(), 1, paths);
    }
#endif
    return context;
}

struct TransformCache {
    ContextPtr context = makeProjContext();
    std::unordered_map<std::string, PJ*> operations;

    ~TransformCache()
    {
        for (auto& [key, operation] : operations) proj_destroy(operation);
    }

    PJ* get(const std::string& source, const std::string& target)
    {
        const std::string key = source + '\n' + target;
        if (const auto found = operations.find(key); found != operations.end())
            return found->second;
        if (!context) return nullptr;
        ProjPtr raw(proj_create_crs_to_crs(context.get(), source.c_str(), target.c_str(), nullptr),
                    proj_destroy);
        if (!raw) return nullptr;
        PJ* operation = proj_normalize_for_visualization(context.get(), raw.get());
        if (!operation) return nullptr;
        operations.emplace(key, operation);
        return operation;
    }
};

std::string upperTrimmed(std::string_view text)
{
    size_t start = 0;
    size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    std::string out;
    out.reserve(end - start);
    for (size_t i = start; i < end; ++i)
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    return out;
}

std::optional<int> parseInteger(std::string_view text)
{
    if (text.empty()) return std::nullopt;
    int value = 0;
    for (const char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return std::nullopt;
        value = value * 10 + (ch - '0');
    }
    return value;
}

std::string projCrsId(std::string_view id)
{
    const std::string norm = upperTrimmed(id);
    if (norm.rfind("UTM:", 0) != 0) return norm;
    const std::string_view body(norm.data() + 4, norm.size() - 4);
    if (body.size() < 2) return {};
    const auto zone = parseInteger(body.substr(0, body.size() - 1));
    const char hemisphere = body.back();
    if (!zone || *zone < 1 || *zone > 60 || (hemisphere != 'N' && hemisphere != 'S'))
        return {};
    return "EPSG:" + std::to_string((hemisphere == 'N' ? 32600 : 32700) + *zone);
}

core::SpatialRef effectiveNavSpatialRef(const core::NavPoint& nav)
{
    return nav.spatial_ref.empty() ? spatialRefFromLegacy(nav.is_projected) : nav.spatial_ref;
}

bool transformCoordinate(std::string_view source_id, std::string_view target_id,
                         double source_x, double source_y,
                         double& target_x, double& target_y)
{
    if (!std::isfinite(source_x) || !std::isfinite(source_y)) return false;
    const std::string source = projCrsId(source_id);
    const std::string target = projCrsId(target_id);
    if (source.empty() || target.empty()) return false;
    if (source == target) {
        target_x = source_x;
        target_y = source_y;
        return true;
    }

    thread_local TransformCache cache;
    PJ* operation = cache.get(source, target);
    if (!operation) return false;

    proj_errno_reset(operation);
    const PJ_COORD result = proj_trans(operation, PJ_FWD,
                                       proj_coord(source_x, source_y, 0.0, 0.0));
    if (proj_errno(operation) != 0 || !std::isfinite(result.xy.x)
        || !std::isfinite(result.xy.y))
        return false;
    target_x = result.xy.x;
    target_y = result.xy.y;
    return true;
}

const geod_geodesic& wgs84Geodesic()
{
    static const geod_geodesic geodesic = [] {
        geod_geodesic value{};
        geod_init(&value, kWgs84A, kWgs84F);
        return value;
    }();
    return geodesic;
}

bool validGeographic(double lat_deg, double lon_deg)
{
    return std::isfinite(lat_deg) && std::isfinite(lon_deg)
        && lat_deg >= -90.0 && lat_deg <= 90.0;
}

} // namespace

bool isFiniteCoordinate(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon);
}

bool isFiniteNav(const core::NavPoint& nav)
{
    return nav.valid && isFiniteCoordinate(nav.lat, nav.lon);
}

double wrapLongitude180(double lon_deg) noexcept
{
    if (!std::isfinite(lon_deg)) return lon_deg;
    double wrapped = std::fmod(lon_deg + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

double unwrapLongitudeNear(double lon_deg, double reference_deg) noexcept
{
    if (!std::isfinite(lon_deg) || !std::isfinite(reference_deg)) return lon_deg;
    return reference_deg + std::remainder(lon_deg - reference_deg, 360.0);
}

bool navUsesProjectedCoordinates(const core::NavPoint& nav)
{
    return core::spatialRefIsProjected(effectiveNavSpatialRef(nav));
}

double haversineMetres(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
    if (!validGeographic(lat1_deg, lon1_deg) || !validGeographic(lat2_deg, lon2_deg))
        return std::numeric_limits<double>::quiet_NaN();
    double distance_m = 0.0;
    geod_inverse(&wgs84Geodesic(), lat1_deg, lon1_deg, lat2_deg, lon2_deg,
                 &distance_m, nullptr, nullptr);
    return distance_m;
}

double navDistanceMetres(const core::NavPoint& a, const core::NavPoint& b)
{
    if (!isFiniteNav(a) || !isFiniteNav(b))
        return std::numeric_limits<double>::quiet_NaN();
    const auto a_ref = effectiveNavSpatialRef(a);
    const auto b_ref = effectiveNavSpatialRef(b);
    if (core::spatialRefIsProjected(a_ref) && a_ref.id == b_ref.id)
        return std::hypot(b.lon - a.lon, b.lat - a.lat);

    core::NavPoint a_wgs84;
    core::NavPoint b_wgs84;
    const auto wgs84 = core::makeWgs84SpatialRef();
    if (!normalizeNavForMap(a, wgs84, a_wgs84)
        || !normalizeNavForMap(b, wgs84, b_wgs84))
        return std::numeric_limits<double>::quiet_NaN();
    return haversineMetres(a_wgs84.lat, a_wgs84.lon, b_wgs84.lat, b_wgs84.lon);
}

core::SpatialRef spatialRefFromId(std::string_view id)
{
    const std::string norm = upperTrimmed(id);
    if (norm.empty()) return {};
    if (norm == "EPSG:4326") return core::makeWgs84SpatialRef();
    if (norm == "DISPLAY:PSEUDO_WGS84") return core::makePseudoWgs84SpatialRef();
    if (norm.rfind("LOCAL:", 0) == 0) {
        core::SpatialRef ref{norm, core::SpatialRefKind::Local, false};
        return ref;
    }
    if (norm.rfind("PROJECTED:", 0) == 0)
        return core::makeUnknownProjectedSpatialRef(norm);

    const std::string crs_id = projCrsId(norm);
    ContextPtr context = makeProjContext();
    ProjPtr crs(context && !crs_id.empty() ? proj_create(context.get(), crs_id.c_str()) : nullptr,
                proj_destroy);
    core::SpatialRef ref;
    ref.id = norm;
    if (!crs) return ref;
    switch (proj_get_type(crs.get())) {
    case PJ_TYPE_GEOGRAPHIC_2D_CRS:
    case PJ_TYPE_GEOGRAPHIC_3D_CRS:
        ref.kind = core::SpatialRefKind::Geographic;
        break;
    case PJ_TYPE_PROJECTED_CRS:
        ref.kind = core::SpatialRefKind::Projected;
        break;
    default:
        ref.kind = core::SpatialRefKind::Unknown;
        break;
    }
    ref.exact = ref.kind != core::SpatialRefKind::Unknown;
    return ref;
}

bool isTransformableCrs(const core::SpatialRef& ref)
{
    if (ref.empty() || ref.kind == core::SpatialRefKind::Unknown
        || ref.kind == core::SpatialRefKind::Local
        || ref.id == "DISPLAY:PSEUDO_WGS84")
        return false;
    double x = 0.0;
    double y = 0.0;
    return transformCoordinate(ref.id, "EPSG:4326", 0.0, 0.0, x, y);
}

double headingFromNavDeltaRad(const core::NavPoint& from, const core::NavPoint& to)
{
    if (!isFiniteNav(from) || !isFiniteNav(to)) return 0.0;
    const auto from_ref = effectiveNavSpatialRef(from);
    const auto to_ref = effectiveNavSpatialRef(to);
    if (core::spatialRefIsProjected(from_ref) && from_ref.id == to_ref.id) {
        const double east = to.lon - from.lon;
        const double north = to.lat - from.lat;
        return east == 0.0 && north == 0.0 ? 0.0 : std::atan2(east, north);
    }
    core::NavPoint a;
    core::NavPoint b;
    const auto wgs84 = core::makeWgs84SpatialRef();
    if (!normalizeNavForMap(from, wgs84, a) || !normalizeNavForMap(to, wgs84, b))
        return 0.0;
    double distance_m = 0.0;
    double azimuth_deg = 0.0;
    geod_inverse(&wgs84Geodesic(), a.lat, a.lon, b.lat, b.lon,
                 &distance_m, &azimuth_deg, nullptr);
    return distance_m == 0.0 ? 0.0 : azimuth_deg / kRadToDeg;
}

double blendAngleRad(double previous, double next, double alpha)
{
    alpha = std::clamp(alpha, 0.0, 1.0);
    const double delta = std::atan2(std::sin(next - previous), std::cos(next - previous));
    return previous + delta * alpha;
}

bool latLonToProjected(double lat_deg, double lon_deg, const core::SpatialRef& target,
                       double& northing_out, double& easting_out)
{
    const auto resolved_target = target.kind == core::SpatialRefKind::Unknown
        ? spatialRefFromId(target.id) : target;
    if (!validGeographic(lat_deg, lon_deg) || !core::spatialRefIsProjected(resolved_target))
        return false;
    return transformCoordinate("EPSG:4326", resolved_target.id, lon_deg, lat_deg,
                               easting_out, northing_out);
}

bool latLonToUtm(double lat_deg, double lon_deg, int& zone_out, bool& north_out,
                 double& easting_out, double& northing_out)
{
    if (!validGeographic(lat_deg, lon_deg) || lon_deg < -180.0 || lon_deg > 180.0
        || lat_deg < kUtmMinLat || lat_deg > kUtmMaxLat)
        return false;
    zone_out = std::min(60, static_cast<int>((lon_deg + 180.0) / 6.0) + 1);
    north_out = lat_deg >= 0.0;
    const auto target = spatialRefFromId("EPSG:" + std::to_string(
        (north_out ? 32600 : 32700) + zone_out));
    return latLonToProjected(lat_deg, lon_deg, target, northing_out, easting_out);
}

bool offsetNavByGroundMetres(const core::NavPoint& nav, double east_m, double north_m,
                             double& out_lon, double& out_lat)
{
    if (!isFiniteNav(nav) || !std::isfinite(east_m) || !std::isfinite(north_m)) return false;
    if (navUsesProjectedCoordinates(nav)) {
        out_lon = nav.lon + east_m;
        out_lat = nav.lat + north_m;
        return isFiniteCoordinate(out_lat, out_lon);
    }
    if (!validGeographic(nav.lat, nav.lon)) return false;
    const double distance_m = std::hypot(east_m, north_m);
    if (distance_m == 0.0) {
        out_lon = nav.lon;
        out_lat = nav.lat;
        return true;
    }
    const double azimuth_deg = std::atan2(east_m, north_m) * kRadToDeg;
    geod_direct(&wgs84Geodesic(), nav.lat, nav.lon, azimuth_deg, distance_m,
                &out_lat, &out_lon, nullptr);
    // Preserve the caller's continuous longitude branch across the date line.
    out_lon = unwrapLongitudeNear(out_lon, nav.lon);
    return validGeographic(out_lat, out_lon);
}

bool normalizeNavForMap(const core::NavPoint& input, const core::SpatialRef& display_ref,
                        core::NavPoint& output)
{
    output = input;
    if (!isFiniteCoordinate(input.lat, input.lon)) return false;
    const auto source = effectiveNavSpatialRef(input);
    const auto target = display_ref.empty() ? core::makeWgs84SpatialRef() : display_ref;
    if (source.empty() || target.empty() || source.id == "DISPLAY:PSEUDO_WGS84"
        || target.id == "DISPLAY:PSEUDO_WGS84")
        return false;

    double x = 0.0;
    double y = 0.0;
    if (!transformCoordinate(source.id, target.id, input.lon, input.lat, x, y)) return false;
    output.lon = x;
    output.lat = y;
    output.spatial_ref = target;
    output.is_projected = core::spatialRefIsProjected(target);
    output.valid = isFiniteCoordinate(y, x);
    return output.valid;
}

} // namespace dolphin::geo
