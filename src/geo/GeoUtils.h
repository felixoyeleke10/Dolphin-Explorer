#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/NavPoint.h"
#include "core/SidescanPing.h"
#include "core/SpatialRef.h"

namespace dolphin::geo {

bool isFiniteCoordinate(double lat, double lon);
bool isFiniteNav(const core::NavPoint& nav);
bool   navUsesProjectedCoordinates(const core::NavPoint& nav);
double haversineMetres(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
double navDistanceMetres(const core::NavPoint& a, const core::NavPoint& b);

// Canonical public longitude and local unwrapping helpers. Internal map geometry
// may use a continuous branch outside [-180, 180) to cross the antimeridian
// without a world-spanning discontinuity; UI/export boundaries wrap it again.
double wrapLongitude180(double lon_deg) noexcept;
double unwrapLongitudeNear(double lon_deg, double reference_deg) noexcept;

core::SpatialRef spatialRefFromId(std::string_view id);
core::SpatialRef spatialRefFromLegacy(bool is_projected);
std::string      spatialRefKindToString(core::SpatialRefKind kind);
core::SpatialRefKind spatialRefKindFromString(std::string_view text);

// True if normalizeNavForMap can transform ref to/from WGS84.
// Geographic CRS always return true (treated as ≈ WGS84).
// Projected: only UTM zones (WGS84, ETRS89, NAD83, GDA94, GDA2020) return true.
bool isTransformableCrs(const core::SpatialRef& ref);

double headingFromNavDeltaRad(const core::NavPoint& from, const core::NavPoint& to);
double blendAngleRad(double previous, double next, double alpha);

// Forward UTM projection (WGS84). Zone is auto-determined from lon_deg.
// Returns false for non-finite or out-of-range input.
bool latLonToUtm(double lat_deg, double lon_deg,
                 int& zone_out, bool& north_out,
                 double& easting_out, double& northing_out);

// Forward-project geographic WGS84 lat/lon into a target projected CRS, using
// the target's own zone (not auto-derived from longitude). Only UTM-family
// targets recognised by parseUtmZone are supported (same set as
// isTransformableCrs); returns false for geographic/unsupported targets so the
// caller can fall back to a geographic readout. Outputs metres.
bool latLonToProjected(double lat_deg, double lon_deg,
                       const core::SpatialRef& target,
                       double& northing_out, double& easting_out);

bool offsetNavByGroundMetres(const core::NavPoint& nav,
                             double east_m,
                             double north_m,
                             double& out_lon,
                             double& out_lat);

bool normalizeNavForMap(const core::NavPoint& input,
                        const core::SpatialRef& display_ref,
                        core::NavPoint&       output);

// Takes ownership of raw to avoid a full copy — pass std::move(raw) at the call site.
// If out_unresolved is non-null, it receives one SpatialRef per unique source CRS
// that could not be transformed (triggers pseudo-degree fallback).
std::vector<core::SidescanPing>
normalizeSidescanPingsForMap(std::vector<core::SidescanPing>  raw,
                             const core::SpatialRef&          display_ref,
                             std::vector<core::SpatialRef>*   out_unresolved = nullptr);

} // namespace dolphin::geo
