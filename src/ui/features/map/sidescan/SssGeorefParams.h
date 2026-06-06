#pragma once
#include "core/SidescanPing.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace dolphin::ui {

// -- Nav position source -------------------------------------------------------

enum class SssNavPositionSource {
    Auto,          // fish/sensor position if valid → vessel/ship position
    FishSensor,    // fish/sensor GPS only; skip ping if unavailable
    VesselShip,    // vessel/ship GPS only; skip ping if unavailable
    VesselLayback, // vessel GPS + layback offset along vessel heading
    ManualOffset,  // best available position + user x/y offset
};

// -- Heading source ------------------------------------------------------------

enum class SssHeadingSource {
    Auto,                     // sensor_heading → ship_heading → smoothed COG
    FishSensor,               // sensor_heading_deg only; skip ping if unavailable
    VesselShip,               // ship_heading_deg only; skip ping if unavailable
    CourseOverGround,         // instantaneous COG from consecutive nav positions
    SmoothedCourseOverGround, // EMA-smoothed COG (closest to original behaviour)
};

// -- Nav smoothing -------------------------------------------------------------

enum class SssNavSmoothingMode {
    Off,            // no smoothing — use raw resolved positions as-is
    SpikeRejection, // skip positions that jump past the spike distance threshold
    MovingAverage,  // simple window average over smoothing_window pings
    Median,         // window median (falls back to MovingAverage in this build)
};

// -- Processing parameters -----------------------------------------------------

struct SssGeorefParams {
    // Nav position source
    SssNavPositionSource nav_source      = SssNavPositionSource::Auto;

    // Heading source
    SssHeadingSource heading_source      = SssHeadingSource::Auto;

    // Layback (used when nav_source == VesselLayback)
    bool   enable_layback   = false;
    bool   use_file_layback = true;    // true → use ping.layback_m; false → manual_layback_m
    double manual_layback_m = 0.0;

    // Manual offsets applied after nav source selection (metres, east/north)
    double x_offset_m        = 0.0;
    double y_offset_m        = 0.0;
    double heading_offset_deg = 0.0;

    // Port / starboard
    bool   swap_port_starboard = false;  // emergency flip for inverted files

    // Nav smoothing
    SssNavSmoothingMode smoothing_mode   = SssNavSmoothingMode::Off;
    int                 smoothing_window = 5;

    // Debug flags
    bool debug_ping_lines_only = false;  // render individual sample dots; skip quad stitching

    // Display geometry
    // When false the swath is rendered "opened": the inner (nadir) edge is
    // offset from the vessel track by the near-nadir dead zone, leaving a
    // visible gap that represents the unprocessed water column.
    // When true the gap is closed — the inner edge sits on the vessel track.
    bool slant_range_corrected = false;
};

// -- Per-file diagnostics ------------------------------------------------------

struct HeadingStats {
    size_t from_sensor = 0;
    size_t from_ship   = 0;
    size_t from_cog    = 0;
    size_t skipped     = 0;
};

// -- Resolved values -----------------------------------------------------------

struct ResolvedPosition {
    double lat = 0.0, lon = 0.0;
    bool   valid        = false;
    bool   is_projected = false;
    core::SpatialRef spatial_ref;
    uint32_t flags      = 0;
};

// Flag bits stored in ResolvedPosition::flags
constexpr uint32_t kNavFlagFishPos      = 1u << 0;  // fish/sensor position used
constexpr uint32_t kNavFlagVesselPos    = 1u << 1;  // vessel/ship position used
constexpr uint32_t kNavFlagLayback      = 1u << 2;  // layback offset applied
constexpr uint32_t kNavFlagManualOffset = 1u << 3;  // manual x/y offset applied

struct ResolvedHeading {
    double           heading_rad = std::numeric_limits<double>::quiet_NaN();
    bool             valid       = false;
    SssHeadingSource source      = SssHeadingSource::Auto;
};

struct CorrectedSssNav {
    double lat = 0.0, lon = 0.0;
    bool   valid         = false;
    double heading_rad   = std::numeric_limits<double>::quiet_NaN();
    bool   heading_valid = false;
    bool   is_projected  = false;
    core::SpatialRef spatial_ref;
    uint32_t flags       = 0;
};

// -- Per-ping resolution functions ---------------------------------------------

// Select fish / vessel / layback position for one ping.
// heading_rad is consumed only when nav_source == VesselLayback; pass NaN
// if the heading is not yet known (layback will be skipped).
ResolvedPosition resolveSssPosition(
    const core::SidescanPing& ping,
    const SssGeorefParams&    params,
    double heading_rad = std::numeric_limits<double>::quiet_NaN());

// Select heading for one ping given pre-computed COG values from the caller.
// Applies heading_offset_deg. Does not do EMA smoothing — use buildHeadingTable
// for a full table with smoothing and backward-fill.
ResolvedHeading resolveSssHeading(
    const core::NavPoint&  nav,
    const SssGeorefParams& params,
    double cog_rad          = std::numeric_limits<double>::quiet_NaN(),
    double smoothed_cog_rad = std::numeric_limits<double>::quiet_NaN());

// -- Full corrected nav table ---------------------------------------------------
//
// Returns one CorrectedSssNav per ping, indexed to pings[order[i]].
// Resolves heading (with EMA/COG and backward-fill) then position (with
// nav_source, layback, and manual offsets), then applies nav smoothing.
// Both georeferenceSidescanPings() and buildSwathCoverage() must call this
// function so mosaic and coverage share identical per-ping positions and headings.
std::vector<CorrectedSssNav> buildCorrectedNavTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    HeadingStats*                          out_stats = nullptr);

// -- Heading-only table (backward compatibility) -------------------------------
//
// Returns heading_rad per ping (NaN = unresolvable).
// Kept for callers that do not yet use buildCorrectedNavTable.
std::vector<double> buildHeadingTable(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>&             order,
    const SssGeorefParams&                 params,
    HeadingStats*                          out_stats = nullptr);

} // namespace dolphin::ui
