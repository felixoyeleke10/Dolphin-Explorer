#pragma once
#include "core/SpatialRef.h"
#include <string>
#include <vector>

namespace dolphin::io {

// Lightweight summary produced by a reader's probe() method.
// Contains everything the import wizard needs for its Summary and CRS tabs
// without building a full seek table.
struct ProbeResult {
    std::string path;
    std::string format_name;    // "SEG-Y", "XTF", "JSF"

    // Detected modalities
    bool has_sidescan     = false;
    bool has_subbottom    = false;
    bool has_magnetometer = false;
    bool has_multibeam    = false;

    // Coordinate / CRS state
    bool             is_projected       = false;  // true = projected metres
    bool             coord_valid        = false;  // at least one non-zero coord found
    bool             needs_crs_review   = false;  // confidence low → show CRS tab entry
    bool             possibly_swapped   = false;  // X/Y fields likely transposed
    bool             units_contradicted = false;  // declared geo but projected magnitudes
    core::SpatialRef declared_crs;                // from file header; may be empty

    // Coordinate extent in file-native units (degrees or metres)
    double coord_min_x = 0.0, coord_max_x = 0.0;
    double coord_min_y = 0.0, coord_max_y = 0.0;

    // Up to 5 representative coordinate samples for CRS tab display
    struct CoordSample { double x, y; };
    std::vector<CoordSample> coord_samples;

    // Count heuristics
    int64_t  file_size_bytes       = 0;
    uint32_t estimated_record_count = 0;

    // Heading / attitude (from nav records during probe scan)
    bool  heading_valid = false;
    float heading_min   = 0.f;
    float heading_max   = 0.f;
    float heading_mean  = 0.f;

    // Channel table (populated from file header, not per-ping scan)
    struct ChannelInfo {
        std::string name;             // "Port Sidescan", "Sub-Bottom", etc.
        std::string modality;         // "Sidescan", "Sub-Bottom", "Magnetometer"
        float       frequency_khz = 0.f;
        float       range_m       = 0.f;
        uint32_t    samples_per_ping = 0;
    };
    std::vector<ChannelInfo> channels;

    // SEG-Y text header excerpt (first ~320 chars)
    std::string text_header_excerpt;

    // Short human-readable warnings for Summary tab display
    std::vector<std::string> warnings;

    // Success / failure
    bool        success = false;
    std::string error_message;
};

} // namespace dolphin::io
