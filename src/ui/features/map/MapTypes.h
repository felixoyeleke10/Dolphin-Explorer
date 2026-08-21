#pragma once
#include <string>
#include <vector>
#include <QImage>
#include <QPointF>
#include <QRect>
#include "core/SidescanPing.h"

namespace dolphin::ui {

// -- Map sonar preview quality -------------------------------------------------
// Controls whether a raster sonar image is built for the map view and at what
// fidelity. Persisted in QSettings ("display/mapSonarQuality").
// Default: CoverageOnly — fast, stable, always available.
//
// Off          — nav track only; no coverage, no image
// CoverageOnly — ribbon swath footprints + nav track; no amplitude raster
// Low          — balanced (1024 px, 1024 groups / 2048 records, 256 samples)
// Medium       — detailed (2048 px, 2048 groups / 4096 records, 512 samples)
// High         — maximum map fidelity (4096 px, 4096 records, 1024 samples)
//
// The three image tiers keep their original enum NUMBERS (3/4/5) — they were once
// labelled Medium/High/Full, renamed to Low/Medium/High — so persisted settings keep
// the same actual resolution, just under a new name. Value 2 (a still-earlier "Low")
// is retired; mapSonarQualityFromInt() migrates any stale value to the lowest image
// tier (Low).
enum class MapSonarQuality : int {
    Off          = 0,
    CoverageOnly = 1,
    Low          = 3,   // formerly "Medium"
    Medium       = 4,   // formerly "High"
    High         = 5,   // formerly "Full / Best Available"
};

// Map a persisted quality int to a current value. Valid values pass through; any
// retired/unknown value migrates to the lowest image tier (Low).
inline MapSonarQuality mapSonarQualityFromInt(int v)
{
    switch (static_cast<MapSonarQuality>(v)) {
        case MapSonarQuality::Off:
        case MapSonarQuality::CoverageOnly:
        case MapSonarQuality::Low:
        case MapSonarQuality::Medium:
        case MapSonarQuality::High:
            return static_cast<MapSonarQuality>(v);
        default:
            return MapSonarQuality::Low;
    }
}

// -- Coverage display (always built for CoverageOnly+, fast, no amplitude) -----
struct SwathCoverage {
    core::SidescanChannel channel = core::SidescanChannel::Port;

    // Ribbon polygons in display CRS (lon/lat degrees or projected metres).
    // Each polygon is closed: inner_edge[0..n-1] then outer_edge[n-1..0].
    std::vector<std::vector<QPointF>> ribbons;
};

// One physically resolved across-track ray for a single channel record. Unlike
// coverage ribbons, this retains ping identity and the true sonar position.
struct SidescanBeamRay {
    core::SidescanChannel channel = core::SidescanChannel::Port;
    int64_t               timestamp_us = 0;
    uint32_t              ping_number = 0;
    QPointF               origin;
    QPointF               outer;
};

// -- Mosaic types (reserved for future on-demand tiled mosaic builder) ---------
//
// NOT built automatically. A future "Build Mosaic" action will generate these
// in a background job with progress, cancel, memory budget, and export.

struct SssPoint {
    double   lon       = 0.0;
    double   lat       = 0.0;
    uint16_t amplitude = 0;
    // Authoritative across-track ground range used to align adjacent strips.
    // This avoids warping unequal/non-uniform sample grids by point index.
    float    ground_range_m = 0.0f;
    // Geometry-only anchors preserve a valid swath footprint without burning
    // a synthetic bottom-return pixel into the mosaic/drape.
    bool     renderable = true;
};

struct SssStrip {
    core::SidescanChannel channel      = core::SidescanChannel::Port;
    int64_t               timestamp_us = 0;    // ping timestamp (µs since Unix epoch)
    uint32_t              ping_number  = 0;    // XTF PingNumber; 0 = unknown (JSF)
    double                nav_lon      = 0.0;  // vessel centre position (from ping.nav)
    double                nav_lat      = 0.0;
    // Per-channel source segment. Explicit decoded-but-unusable records advance
    // it so raster stitching cannot silently paint across rejected data.
    uint64_t              continuity_segment = 0;
    std::vector<SssPoint> points;
};

// -- Nav quality statistics ----------------------------------------------------
// Collected by buildSwathNavTrack from the raw ping set. Displayed as
// warning badges in the map view and used for QC diagnostics.
struct NavStats {
    // total_pings = loaded channel records (port + stbd combined, after thinning).
    // pings_available = ping groups in the original index = total entries / 2 (floor).
    // The two counts differ by definition: total_pings counts individual channel records;
    // pings_available counts paired groups. An odd entry count produces an off-by-one.
    size_t total_pings    = 0;   // channel records loaded (both channels combined)
    size_t invalid_nav    = 0;   // pings with missing / unusable nav position
    size_t interpolated_nav = 0; // pings repaired between trustworthy timed anchors
    size_t repeated_fixes = 0;   // consecutive identical GPS fixes
    size_t nav_spikes     = 0;   // ping-to-ping jumps rejected as GPS spikes
    size_t time_gaps      = 0;   // timestamp gaps > 5 s (survey line breaks)
    double avg_spacing_m  = 0.0; // mean ping-to-ping ground distance
    double max_spacing_m  = 0.0; // largest single ping-to-ping ground distance

    // Per-pair stitch reject counters (populated by buildSwathPreviewImage).
    size_t stitch_nav_rejects     = 0;  // pairs rejected by nav-centre distance
    size_t stitch_time_rejects    = 0;  // pairs rejected by timestamp gap
    size_t stitch_ping_rejects    = 0;  // pairs rejected by ping-number gap
    size_t stitch_heading_rejects = 0;  // pairs rejected by heading change

    // Per-ping skip counters (populated by buildSwathPreviewImage via georeferencer).
    size_t skipped_no_position  = 0;  // pings skipped: no valid resolved position
    size_t skipped_no_heading   = 0;  // pings skipped: no valid resolved heading
    size_t skipped_no_samples   = 0;  // pings skipped: empty sample array

    // Build output counts.
    size_t strips_built           = 0;  // georeferenced strips that entered rasterization
    size_t cells_attempted        = 0;  // quadrilateral cells submitted to rasterizer
    size_t cells_rasterized       = 0;  // valid cells that wrote at least one pixel
    size_t preview_pixels_written = 0;  // non-transparent pixels after rasterization (raw)
    size_t preview_pixels_filled  = 0;  // legacy post-fill count; current rasterizer leaves this 0
    size_t coverage_ribbons_built = 0;  // ribbon polygons across all channels

    // Spatial placement diagnostics (for diagnosing blank / misplaced map).
    // nav bbox  = padded bbox used to size and place the QImage (from buildSwathNavTrack).
    // strip bbox = actual centre positions of georeferenced strips (from buildSwathPreviewImage).
    // A large discrepancy between the two means the image is placed in the wrong area.
    double nav_lon_min   =  1e18, nav_lon_max = -1e18;   // image placement bounds
    double nav_lat_min   =  1e18, nav_lat_max = -1e18;
    double strip_lon_min =  1e18, strip_lon_max = -1e18;  // where strips actually fell
    double strip_lat_min =  1e18, strip_lat_max = -1e18;
    int    image_width   = 0;   // QImage pixel dimensions
    int    image_height  = 0;

    // -- Build parameters (set by SidescanViewController in background task) -----
    MapSonarQuality quality_used    = MapSonarQuality::Low;
    size_t          pings_available = 0;    // total ping groups in store (before thinning)
    bool            memory_reduced  = false; // true if raster dimensions hit memory ceiling
    std::string     crs_label;              // e.g. "EPSG:32632 projected exact"
    std::string     unsupported_crs_id;     // non-empty if any pings used pseudo-degree fallback

    // -- View state (set post-placement on main thread) ------------------------
    bool  layer_visible  = true;
    bool  layer_active   = true;
    bool  fit_applied    = false;   // true if auto-fit ran; false = user had interacted
    QRect paint_rect;               // pixel rect of the preview image on screen
    bool  paint_onscreen = false;   // paint_rect intersects the current viewport
};

// -- Track nav quality statistics ---------------------------------------------
// Collected by buildTrackLayerMapData (and future track-type builders for
// MAG, MBE, etc.).  Parallels NavStats but covers only the fields meaningful for
// a single-modality nav track — no swath, heading, or image concepts.
struct TrackStats {
    // Input accounting
    size_t total_traces   = 0;   // ArtifactIndex entries of the correct type
    size_t invalid_nav    = 0;   // non-finite or normalisation-failed coordinates
    size_t zero_coords    = 0;   // lat==0 && lon==0 (recording gap sentinel)
    size_t repeated_fixes = 0;   // skipped: identical to the previous position
    size_t large_jumps    = 0;   // gaps above threshold → NaN segment break inserted

    // Track output
    size_t segment_count  = 0;   // continuous segments (0 = empty, 1 = unbroken)
    size_t track_points   = 0;   // points written to nav_track (excl. NaN breaks)

    // Spacing statistics in metres (0.0 when fewer than 2 points)
    double avg_spacing_m  = 0.0;
    double max_spacing_m  = 0.0;

    // First / last accepted position in display CRS
    double first_lon = 0.0, first_lat = 0.0;
    double last_lon  = 0.0, last_lat  = 0.0;
    bool   has_endpoints = false;

    // CRS / spatial
    std::string crs_label;    // e.g. "EPSG:32630 projected exact"
    bool        is_projected = false;

    // View state (filled on the main thread after the build task completes)
    bool layer_visible = true;
};

struct LayerMosaic {
    QImage  image;
    double  lon0       = 0.0;
    double  lat0       = 0.0;   // north (top) edge
    double  deg_per_px = 1.0;
    bool    valid      = false;
};

// -- Layer map kind ------------------------------------------------------------
// Describes what kind of geometry a LayerMapData entry contributes to the map.
// Set by the builder (buildSwathNavTrack, buildTrackLayerMapData, etc.);
// used by paint and diagnostics code to apply modality-appropriate rendering.
enum class LayerMapKind : uint8_t {
    Unknown = 0,  // default; treat as Track for rendering
    Track   = 1,  // nav track only (MAG, MBE)
    Swath   = 2,  // coverage ribbons + optional sonar image (SSS)
    Profile = 3,  // vertical acoustic profile: nav track + per-trace scalar (SBP)
};

// -- Per-layer data held by MapView --------------------------------------------

struct LayerMapData {
    LayerMapKind kind = LayerMapKind::Unknown;

    // Coverage footprints (port + starboard swath outlines).
    std::vector<SwathCoverage> coverage;

    // Per-ping overlay geometry generated from the same corrected pose and
    // slant/ground-range policy used by the mosaic.
    std::vector<SidescanBeamRay> beam_rays;

    // Nav track: (lon, lat) points in display CRS; NaN = segment break.
    std::vector<QPointF> nav_track;

    // Bounding box in display CRS.
    double lon_min =  1e18, lon_max = -1e18;
    double lat_min =  1e18, lat_max = -1e18;
    bool   is_projected   = false;
    bool   visible        = true;
    bool   show_nav_track = false;
    // Per-layer map transparency [0,1] (mirrors DataLayer::map_opacity; seeded
    // by MapView::setLayerMapData from the project, live-updated via
    // MapView::setLayerOpacity).
    float  opacity        = 1.f;
    // Per-layer mosaic blend mode (mirrors DataLayer::map_blend_mode):
    // 0=Blend, 1=Cover up, 2=Lighten, 3=Darken. Controls how this layer's
    // sonar image composites over layers already drawn beneath it (visible
    // where surveys overlap).
    int    blend_mode     = 0;
    // Clip this layer's mosaic to the project's drawn polygons (show inside).
    bool   clip_polygons  = false;
    // Draw the across-track beam fan (nadir → swath edge lines) over the mosaic.
    bool   show_beams     = false;
    // Step between drawn beam lines (1=densest, higher=sparser).
    int    beam_spacing   = 10;

    // Sonar amplitude preview image (quality >= Low only; null otherwise).
    // Drawn below coverage ribbons and nav track in the map view.
    // The image covers exactly [lon_min, lon_max] × [lat_min, lat_max].
    QImage   preview_image;
    bool     preview_reduced = false;  // true: image downgraded due to memory cap

    // Greyscale intensity cache for zero-cost palette recoloring.
    // Same pixel layout as preview_image (row-major, intensity_w × intensity_h).
    // 0 = transparent (no sonar return at that pixel); 1-65535 = raw amplitude.
    // Populated by buildSwathPreviewImage. Used by SidescanViewController to
    // rebuild preview_image on palette change without any disk I/O or geometry work.
    std::vector<uint16_t> intensity_cache;
    int   intensity_w    = 0;
    int   intensity_h    = 0;
    float intensity_disp_low  = 0.f;
    float intensity_disp_high = 1.f;

    // Nav quality statistics populated by buildSwathNavTrack (SSS layers).
    NavStats  nav_stats;

    // Track quality statistics populated by buildTrackLayerMapData (MAG / MBE).
    TrackStats track_stats;

    // -- Profile data (Profile layers / SBP only) ------------------------------
    // One scalar value per nav_track entry; NaN = gap sentinel or no data.
    // Values represent bottom depth in metres (from bottom_sample_idx pick).
    // scalar_min / scalar_max give the range for colorisation.
    std::vector<float> trace_scalar;
    float scalar_min   = 0.f;
    float scalar_max   = 0.f;
    std::string scalar_label;   // e.g. "bottom depth (m)"

    // Peak absolute amplitude [0,1] at the bottom pick per nav_track entry.
    // 0 = no pick or no samples.  Parallel to trace_scalar.
    std::vector<float> trace_amplitude;

    // Real profile raster for the 3D curtain (SBP only): one column per
    // nav_track entry (gap columns transparent), rows = depth 0 →
    // curtain_depth_m. RGBA8888: R = percentile-normalized |amplitude| byte,
    // A = 255 where the trace has a sample at that depth, 0 otherwise. The
    // palette is applied in the curtain shader, so recolouring is a uniform
    // change — no CPU rebuild.
    QImage curtain_image;
    float  curtain_depth_m = 0.f;
};

// -- Viewport state -----------------------------------------------------------
// Canonical scale/rotation state for the active view.
// Owned by ViewportCoordinator; passed through viewportChanged signals.
struct ViewportState {
    double mpp     = 0.0;   // ground metres per screen pixel
    double rot_deg = 0.0;   // bearing / camera yaw (degrees)
};

} // namespace dolphin::ui
