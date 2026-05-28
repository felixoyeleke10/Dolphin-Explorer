#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <optional>
#include <string>

namespace dolphin::geo {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kUtmScale = 0.9996;
constexpr double kFalseEasting = 500000.0;
constexpr double kFalseNorthingSouth = 10000000.0;
constexpr double kMetresPerDegLat = 111320.0;

struct UtmZone {
    int  zone = 0;
    bool north_hemisphere = true;
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
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return std::nullopt;
        value = value * 10 + (ch - '0');
    }
    return value;
}

std::optional<UtmZone> parseUtmZone(std::string_view crs)
{
    const std::string norm = upperTrimmed(crs);
    if (norm.empty()) return std::nullopt;

    if (norm.rfind("EPSG:", 0) == 0) {
        auto code = parseInteger(std::string_view(norm).substr(5));
        if (!code) return std::nullopt;
        // WGS84 UTM (canonical)
        if (*code >= 32601 && *code <= 32660)
            return UtmZone{*code - 32600, true};
        if (*code >= 32701 && *code <= 32760)
            return UtmZone{*code - 32700, false};
        // ETRS89 UTM North (EPSG:25827–25838, zones 27–38) — same ellipsoid as WGS84
        if (*code >= 25827 && *code <= 25838)
            return UtmZone{*code - 25800, true};
        // NAD83 UTM North (EPSG:26901–26923, zones 1–23) — essentially WGS84 ellipsoid
        if (*code >= 26901 && *code <= 26923)
            return UtmZone{*code - 26900, true};
        // GDA94 MGA (EPSG:28349–28356, zones 49–56) — GRS80 ≈ WGS84 at cm level
        if (*code >= 28349 && *code <= 28356)
            return UtmZone{*code - 28300, true};
        // GDA2020 MGA (EPSG:7849–7856, zones 49–56) — GRS80 datum aligned to WGS84
        if (*code >= 7849 && *code <= 7856)
            return UtmZone{*code - 7800, true};
        // ED50 / UTM North (EPSG:23028–23038, zones 28–38) — International 1924 ellipsoid.
        // Approximated with WGS84 constants: ~100-200 m offset in North Sea area, acceptable
        // for chart display. Proper Helmert shift would require a full datum transform library.
        if (*code >= 23028 && *code <= 23038)
            return UtmZone{*code - 23000, true};
        return std::nullopt;
    }

    if (norm.rfind("UTM:", 0) == 0) {
        const std::string_view body(norm.data() + 4, norm.size() - 4);
        if (body.size() < 2) return std::nullopt;
        const char hemi = body.back();
        auto zone = parseInteger(body.substr(0, body.size() - 1));
        if (!zone || *zone < 1 || *zone > 60) return std::nullopt;
        if (hemi != 'N' && hemi != 'S') return std::nullopt;
        return UtmZone{*zone, hemi == 'N'};
    }

    return std::nullopt;
}

} // namespace (anonymous)

// ── Public: latLonToUtm ───────────────────────────────────────────────────────

bool latLonToUtm(double lat_deg, double lon_deg,
                 int& zone_out, bool& north_out,
                 double& easting_out, double& northing_out)
{
    if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg)) return false;
    if (lat_deg < -90.0 || lat_deg > 90.0)   return false;
    if (lon_deg < -180.0 || lon_deg > 180.0) return false;

    const double phi  = lat_deg * kDegToRad;
    const double lam  = lon_deg * kDegToRad;

    const int zone = static_cast<int>((lon_deg + 180.0) / 6.0) + 1;
    const double lam0 = ((static_cast<double>(zone) - 1.0) * 6.0 - 180.0 + 3.0) * kDegToRad;

    const double e2       = kWgs84F * (2.0 - kWgs84F);
    const double e_prime2 = e2 / (1.0 - e2);
    const double sin_phi  = std::sin(phi);
    const double cos_phi  = std::cos(phi);
    const double tan_phi  = std::tan(phi);

    const double N_rad = kWgs84A / std::sqrt(1.0 - e2 * sin_phi * sin_phi);
    const double T     = tan_phi * tan_phi;
    const double C     = e_prime2 * cos_phi * cos_phi;
    const double A     = (lam - lam0) * cos_phi;

    // Meridional arc
    const double M = kWgs84A * (
          (1.0  - e2/4.0 - 3.0*e2*e2/64.0  - 5.0*e2*e2*e2/256.0) * phi
        - (3.0*e2/8.0 + 3.0*e2*e2/32.0 + 45.0*e2*e2*e2/1024.0)   * std::sin(2.0*phi)
        + (15.0*e2*e2/256.0 + 45.0*e2*e2*e2/1024.0)               * std::sin(4.0*phi)
        - (35.0*e2*e2*e2/3072.0)                                   * std::sin(6.0*phi)
    );

    const double A2 = A*A, A3 = A*A2, A4 = A*A3, A5 = A*A4, A6 = A*A5;

    const double easting = kUtmScale * N_rad * (
        A
        + (1.0 - T + C) * A3 / 6.0
        + (5.0 - 18.0*T + T*T + 72.0*C - 58.0*e_prime2) * A5 / 120.0
    ) + kFalseEasting;

    double northing = kUtmScale * (
        M + N_rad * tan_phi * (
              A2 / 2.0
            + (5.0 - T + 9.0*C + 4.0*C*C) * A4 / 24.0
            + (61.0 - 58.0*T + T*T + 600.0*C - 330.0*e_prime2) * A6 / 720.0
        )
    );
    const bool north = lat_deg >= 0.0;
    if (!north) northing += kFalseNorthingSouth;

    if (!std::isfinite(easting) || !std::isfinite(northing)) return false;

    zone_out     = zone;
    north_out    = north;
    easting_out  = easting;
    northing_out = northing;
    return true;
}

namespace {

bool utmToLatLon(const UtmZone& zone,
                 double         easting,
                 double         northing,
                 double&        out_lat_deg,
                 double&        out_lon_deg)
{
    if (!std::isfinite(easting) || !std::isfinite(northing)) return false;
    if (zone.zone < 1 || zone.zone > 60) return false;

    const double e_sq = kWgs84F * (2.0 - kWgs84F);
    const double e_prime_sq = e_sq / (1.0 - e_sq);
    const double e1 = (1.0 - std::sqrt(1.0 - e_sq)) / (1.0 + std::sqrt(1.0 - e_sq));

    const double x = easting - kFalseEasting;
    double y = northing;
    if (!zone.north_hemisphere)
        y -= kFalseNorthingSouth;

    const double lon0 = ((static_cast<double>(zone.zone) - 1.0) * 6.0 - 180.0 + 3.0) * kDegToRad;
    const double m = y / kUtmScale;
    const double mu = m / (kWgs84A * (1.0 - e_sq / 4.0 - 3.0 * e_sq * e_sq / 64.0
                                    - 5.0 * e_sq * e_sq * e_sq / 256.0));

    const double j1 = 3.0 * e1 / 2.0 - 27.0 * std::pow(e1, 3) / 32.0;
    const double j2 = 21.0 * e1 * e1 / 16.0 - 55.0 * std::pow(e1, 4) / 32.0;
    const double j3 = 151.0 * std::pow(e1, 3) / 96.0;
    const double j4 = 1097.0 * std::pow(e1, 4) / 512.0;

    const double fp = mu
        + j1 * std::sin(2.0 * mu)
        + j2 * std::sin(4.0 * mu)
        + j3 * std::sin(6.0 * mu)
        + j4 * std::sin(8.0 * mu);

    const double sin_fp = std::sin(fp);
    const double cos_fp = std::cos(fp);
    const double tan_fp = std::tan(fp);

    const double c1 = e_prime_sq * cos_fp * cos_fp;
    const double t1 = tan_fp * tan_fp;
    const double n1 = kWgs84A / std::sqrt(1.0 - e_sq * sin_fp * sin_fp);
    const double r1 = kWgs84A * (1.0 - e_sq)
        / std::pow(1.0 - e_sq * sin_fp * sin_fp, 1.5);
    const double d = x / (n1 * kUtmScale);

    const double q1 = n1 * tan_fp / r1;
    const double q2 = d * d / 2.0;
    const double q3 = (5.0 + 3.0 * t1 + 10.0 * c1 - 4.0 * c1 * c1 - 9.0 * e_prime_sq)
        * std::pow(d, 4) / 24.0;
    const double q4 = (61.0 + 90.0 * t1 + 298.0 * c1 + 45.0 * t1 * t1
        - 252.0 * e_prime_sq - 3.0 * c1 * c1) * std::pow(d, 6) / 720.0;

    const double q5 = d;
    const double q6 = (1.0 + 2.0 * t1 + c1) * std::pow(d, 3) / 6.0;
    const double q7 = (5.0 - 2.0 * c1 + 28.0 * t1 - 3.0 * c1 * c1
        + 8.0 * e_prime_sq + 24.0 * t1 * t1) * std::pow(d, 5) / 120.0;

    const double lat = fp - q1 * (q2 - q3 + q4);
    const double lon = lon0 + (q5 - q6 + q7) / cos_fp;

    out_lat_deg = lat * kRadToDeg;
    out_lon_deg = lon * kRadToDeg;
    return std::isfinite(out_lat_deg) && std::isfinite(out_lon_deg);
}

core::SpatialRef effectiveNavSpatialRef(const core::NavPoint& nav)
{
    if (!nav.spatial_ref.empty())
        return nav.spatial_ref;
    return spatialRefFromLegacy(nav.is_projected);
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

bool navUsesProjectedCoordinates(const core::NavPoint& nav)
{
    return core::spatialRefIsProjected(effectiveNavSpatialRef(nav));
}

double haversineMetres(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
    constexpr double kEarthR = 6371000.0;
    const double dlat     = (lat2_deg - lat1_deg) * kDegToRad;
    const double dlon     = (lon2_deg - lon1_deg) * kDegToRad;
    const double sin_dlat = std::sin(dlat * 0.5);
    const double sin_dlon = std::sin(dlon * 0.5);
    const double h = sin_dlat * sin_dlat
                   + std::cos(lat1_deg * kDegToRad) * std::cos(lat2_deg * kDegToRad)
                   * sin_dlon * sin_dlon;
    return kEarthR * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}

double navDistanceMetres(const core::NavPoint& a, const core::NavPoint& b)
{
    if (navUsesProjectedCoordinates(a) || navUsesProjectedCoordinates(b))
        return std::hypot(b.lon - a.lon, b.lat - a.lat);
    return haversineMetres(a.lat, a.lon, b.lat, b.lon);
}

core::SpatialRef spatialRefFromId(std::string_view id)
{
    const std::string norm = upperTrimmed(id);
    if (norm.empty())
        return {};

    if (norm == "EPSG:4326") {
        return core::makeWgs84SpatialRef();
    }

    if (norm == "DISPLAY:PSEUDO_WGS84") {
        return core::makePseudoWgs84SpatialRef();
    }

    if (norm.rfind("LOCAL:", 0) == 0) {
        core::SpatialRef ref;
        ref.id = norm;
        ref.kind = core::SpatialRefKind::Local;
        ref.exact = false;
        return ref;
    }

    if (norm.rfind("PROJECTED:", 0) == 0) {
        return core::makeUnknownProjectedSpatialRef(norm);
    }

    if (parseUtmZone(norm)) {
        core::SpatialRef ref;
        ref.id = norm;
        ref.kind = core::SpatialRefKind::Projected;
        ref.exact = true;
        return ref;
    }

    if (norm.rfind("EPSG:", 0) == 0) {
        auto code = parseInteger(std::string_view(norm).substr(5));
        if (code) {
            core::SpatialRef ref;
            ref.id = norm;
            if (*code >= 4000 && *code < 5000) {
                // Geographic CRS: treated as ≈ WGS84 for display purposes.
                ref.kind  = core::SpatialRefKind::Geographic;
                ref.exact = true;
            } else {
                // Projected CRS not caught by parseUtmZone above.
                // We recognise the code but may not have a transform for it.
                // exact=false signals that the user should confirm via Geodesy.
                ref.kind  = core::SpatialRefKind::Projected;
                ref.exact = false;
            }
            return ref;
        }
    }

    core::SpatialRef ref;
    ref.id = norm;
    ref.kind = core::SpatialRefKind::Unknown;
    ref.exact = false;
    return ref;
}

bool isTransformableCrs(const core::SpatialRef& ref)
{
    if (core::spatialRefIsGeographic(ref)) return true;
    if (!core::spatialRefIsProjected(ref)) return false;
    return parseUtmZone(ref.id).has_value();
}

double headingFromNavDeltaRad(const core::NavPoint& from, const core::NavPoint& to)
{
    if (!isFiniteNav(from) || !isFiniteNav(to)) return 0.0;

    double de = to.lon - from.lon;
    double dn = to.lat - from.lat;
    if (!navUsesProjectedCoordinates(from) && !navUsesProjectedCoordinates(to)) {
        const double mean_lat_rad = ((from.lat + to.lat) * 0.5) * kDegToRad;
        de *= std::max(1e-6, std::cos(mean_lat_rad));
    }

    if (de == 0.0 && dn == 0.0) return 0.0;
    return std::atan2(de, dn);
}

double blendAngleRad(double previous, double next, double alpha)
{
    alpha = std::clamp(alpha, 0.0, 1.0);
    const double delta = std::atan2(std::sin(next - previous), std::cos(next - previous));
    return previous + delta * alpha;
}

bool offsetNavByGroundMetres(const core::NavPoint& nav,
                             double               east_m,
                             double               north_m,
                             double&              out_lon,
                             double&              out_lat)
{
    if (!isFiniteNav(nav)) return false;

    if (navUsesProjectedCoordinates(nav)) {
        out_lon = nav.lon + east_m;
        out_lat = nav.lat + north_m;
        return isFiniteCoordinate(out_lat, out_lon);
    }

    const double cos_lat = std::cos(nav.lat * kDegToRad);
    if (std::abs(cos_lat) < 1e-6) return false;
    out_lon = nav.lon + east_m / (kMetresPerDegLat * cos_lat);
    out_lat = nav.lat + north_m / kMetresPerDegLat;
    return isFiniteCoordinate(out_lat, out_lon);
}

bool normalizeNavForMap(const core::NavPoint& input,
                        const core::SpatialRef& display_ref,
                        core::NavPoint&       output)
{
    output = input;
    if (!isFiniteCoordinate(input.lat, input.lon)) return false;

    const core::SpatialRef source_ref = effectiveNavSpatialRef(input);
    const core::SpatialRef target_ref = display_ref.empty()
        ? core::makeWgs84SpatialRef()
        : display_ref;

    if (!navUsesProjectedCoordinates(input)) {
        if (!core::spatialRefIsGeographic(target_ref))
            return false;
        output.spatial_ref = target_ref;
        output.is_projected = false;
        output.valid = isFiniteCoordinate(output.lat, output.lon);
        return output.valid;
    }

    if (target_ref.id == source_ref.id && !target_ref.id.empty()) {
        output.spatial_ref = target_ref;
        output.is_projected = core::spatialRefIsProjected(target_ref);
        output.valid = isFiniteCoordinate(output.lat, output.lon);
        return output.valid;
    }

    auto zone = parseUtmZone(source_ref.id);
    if (!zone) {
        if (!core::spatialRefIsGeographic(target_ref)) {
            // Both source and target are projected: pass raw coordinates through.
            output.is_projected = true;
            output.valid = isFiniteCoordinate(output.lat, output.lon);
            return output.valid;
        }
        // Source is projected but no transform to geographic is available.
        // Return false so the caller can apply the pseudo-degree fallback and
        // post a CRS diagnostic instead of silently displaying raw metres.
        return false;
    }
    if (!core::spatialRefIsGeographic(target_ref))
        return false;

    double lat_deg = 0.0;
    double lon_deg = 0.0;
    if (!utmToLatLon(*zone, input.lon, input.lat, lat_deg, lon_deg))
        return false;

    output.lat = lat_deg;
    output.lon = lon_deg;
    output.spatial_ref = target_ref;
    output.is_projected = false;
    output.valid = isFiniteCoordinate(lat_deg, lon_deg);
    return output.valid;
}

} // namespace dolphin::geo
