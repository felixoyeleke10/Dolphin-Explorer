// TrackMapBuild.cpp — buildTrackLayerMapData implementation.
// Builds nav_track, bbox, and TrackStats from entries of the requested artifact
// type in an ArtifactIndex.  The same loop covers SBP, MAG, MBE, and the
// quick-path fallback that MapView::rebuildNavTrack uses for SSS.
#include "ui/features/map/track/TrackMapBuild.h"
#include "core/Artifact.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

namespace {

constexpr double kMaxSegGapM = 5000.0; // 5 km — universal gap threshold post-normalization
constexpr double kPadGeo       = 0.001;  // ~110 m
constexpr double kPadProjM     = 110.0;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

core::SpatialRef fallbackRefForEntry(const core::ArtifactIndexEntry& entry)
{
    switch (entry.spatial_ref_kind) {
    case core::SpatialRefKind::Geographic:
        return core::makeWgs84SpatialRef();
    case core::SpatialRefKind::Projected:
        return core::makeUnknownProjectedSpatialRef();
    case core::SpatialRefKind::Local: {
        core::SpatialRef ref;
        ref.id   = "LOCAL:UNKNOWN";
        ref.kind = core::SpatialRefKind::Local;
        ref.exact = false;
        return ref;
    }
    default:
        return entry.is_projected ? core::makeUnknownProjectedSpatialRef()
                                  : core::makeWgs84SpatialRef();
    }
}

std::string buildCrsLabel(const core::SpatialRef& ref)
{
    if (ref.empty()) return "unknown";
    const bool is_proj = core::spatialRefIsProjected(ref);
    std::string lbl;
    if (ref.exact) {
        if (!ref.id.empty()) lbl = ref.id + " ";
        lbl += is_proj ? "projected exact" : "geographic exact";
    } else {
        lbl = is_proj ? "inferred projected" : "inferred geographic";
        if (!ref.id.empty()) lbl += " (from " + ref.id + ")";
    }
    return lbl;
}

} // namespace

LayerMapData buildTrackLayerMapData(const core::ArtifactIndex& index,
                                    core::ArtifactType         artifact_type,
                                    const core::SpatialRef&    source_crs,
                                    const core::SpatialRef&    display_crs)
{
    LayerMapData ld;
    ld.kind = LayerMapKind::Track;

    double         prev_lon    = kNaN, prev_lat = kNaN;
    core::NavPoint prev_norm_pt;
    bool           have_prev   = false;
    double         spacing_sum = 0.0;
    size_t         spacing_n   = 0;
    int            n_exact_crs = 0;   // entries that used confirmed source_crs
    int            n_fb_geo    = 0;   // per-entry fallback → geographic
    int            n_fb_proj   = 0;   // per-entry fallback → projected

    for (const auto& entry : index.entries) {
        if (entry.type != artifact_type) continue;
        ++ld.track_stats.total_traces;

        // -- Coordinate validity -------------------------------------------
        if (!geo::isFiniteCoordinate(entry.lat, entry.lon)) {
            ++ld.track_stats.invalid_nav;
            continue;
        }
        if (entry.lat == 0.0 && entry.lon == 0.0) {
            ++ld.track_stats.zero_coords;
            continue;
        }

        // -- CRS normalization ---------------------------------------------
        // Use the file-level CRS only when it has been confirmed by the user
        // (exact = true). An unconfirmed CRS (e.g. "PROJECTED:SEGY") is only
        // a hint from the parser; each entry's own spatial_ref_kind is more
        // reliable and prevents a single bad trace header from forcing the
        // whole file into projected mode.
        core::NavPoint nav;
        nav.lat   = entry.lat;
        nav.lon   = entry.lon;
        nav.valid = true;
        const bool use_exact = !source_crs.empty() && source_crs.exact;
        nav.spatial_ref = use_exact ? source_crs : fallbackRefForEntry(entry);
        nav.is_projected = core::spatialRefIsProjected(nav.spatial_ref)
                           || entry.is_projected;
        if (use_exact)
            ++n_exact_crs;
        else if (core::spatialRefIsProjected(nav.spatial_ref))
            ++n_fb_proj;
        else
            ++n_fb_geo;

        core::NavPoint norm;
        if (!geo::normalizeNavForMap(nav, display_crs, norm)) {
            ++ld.track_stats.invalid_nav;
            continue;
        }

        const double lon = norm.lon;
        const double lat = norm.lat;
        if (norm.is_projected) ld.is_projected = true;

        // -- Repeated-fix filter -------------------------------------------
        if (lon == prev_lon && lat == prev_lat) {
            ++ld.track_stats.repeated_fixes;
            continue;
        }

        // -- Spacing statistics + segment-break detection ------------------
        if (have_prev) {
            const double step_m = geo::navDistanceMetres(prev_norm_pt, norm);
            spacing_sum += step_m;
            ++spacing_n;
            ld.track_stats.max_spacing_m =
                std::max(ld.track_stats.max_spacing_m, step_m);
            if (step_m > kMaxSegGapM) {
                ld.nav_track.push_back({kNaN, kNaN});
                ++ld.track_stats.large_jumps;
            }
        }

        ld.nav_track.push_back({lon, lat});
        ++ld.track_stats.track_points;

        if (!ld.track_stats.has_endpoints) {
            ld.track_stats.first_lon = lon;
            ld.track_stats.first_lat = lat;
            ld.track_stats.has_endpoints = true;
        }
        ld.track_stats.last_lon = lon;
        ld.track_stats.last_lat = lat;

        ld.lon_min = std::min(ld.lon_min, lon);
        ld.lon_max = std::max(ld.lon_max, lon);
        ld.lat_min = std::min(ld.lat_min, lat);
        ld.lat_max = std::max(ld.lat_max, lat);

        prev_lon     = lon;
        prev_lat     = lat;
        prev_norm_pt = norm;
        have_prev    = true;
    }

    // -- Derived statistics ----------------------------------------------------
    if (spacing_n > 0)
        ld.track_stats.avg_spacing_m = spacing_sum / static_cast<double>(spacing_n);

    ld.track_stats.segment_count =
        ld.track_stats.track_points > 0 ? ld.track_stats.large_jumps + 1 : 0;

    ld.track_stats.is_projected = ld.is_projected;
    if (n_exact_crs > 0) {
        // All entries used a confirmed source CRS.
        ld.track_stats.crs_label = buildCrsLabel(source_crs);
    } else if (n_fb_proj > 0 && n_fb_geo > 0) {
        ld.track_stats.crs_label = "mixed per-entry (geo + proj)";
    } else if (n_fb_proj > 0) {
        ld.track_stats.crs_label = "per-entry projected";
    } else if (n_fb_geo > 0) {
        ld.track_stats.crs_label = "per-entry geographic";
    } else {
        ld.track_stats.crs_label = "unknown";
    }

    // -- Bbox padding ----------------------------------------------------------
    if (ld.lon_min < ld.lon_max) {
        const double pad = ld.is_projected ? kPadProjM : kPadGeo;
        ld.lon_min -= pad; ld.lon_max += pad;
        ld.lat_min -= pad; ld.lat_max += pad;
    }

    return ld;
}

} // namespace dolphin::ui
