#pragma once
// SidescanRasterCache — persist a layer's built map raster (intensity grid +
// coverage + nav track + bounds) next to its .dlpd so reopening a project shows
// the line WITHOUT reparsing pings or re-rasterizing. This is the "parse once,
// then work from the raster" model: activateLayer reads this cache first and only
// falls back to the ping path when the cache is missing or stale.
//
// The cache is a DERIVED artifact — safe to delete; it rebuilds on next load.
// Staleness is keyed on the source store fingerprint (size + mtime), the nav
// correction params, slant-range state, and the quality tier. Palette is NOT part
// of the key (the image is recoloured from the persisted intensity grid on load).
#include <cstdint>
#include <string>
#include "ui/features/map/MapTypes.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "app/display/NavProcessingParams.h"
#include "app/display/WaterfallParams.h"

namespace dolphin::ui::rastercache {

// The byte layout and the algorithm that produced the derived raster are
// versioned independently. Bump kCacheFormatVersion when serialization changes;
// bump kRasterAlgorithmRevision whenever geometry or intensity generation changes
// in a way that makes previously generated pixels/coverage unsafe to reuse.
//
// Format v1 had no algorithm field. Format v2 intentionally rejects every v1
// artifact so rasters produced by the legacy stitching/processing path rebuild.
inline constexpr std::uint32_t kCacheFormatVersion      = 5;
inline constexpr std::uint32_t kRasterAlgorithmRevision = 9;

// Identity / freshness key for a cached raster.
struct Meta {
    unsigned long long src_size    = 0;   // store file size  (bytes)
    long long          src_mtime   = 0;   // store file mtime  (ms since epoch)
    unsigned long long nav_hash    = 0;   // hash of nav params + slant-range flag
    int                quality     = 0;   // MapSonarQuality the raster was built at
};

// Status-bar / diagnostics summary that the ping path computes but isn't part of
// LayerMapData — persisted so the cache path can fill the status bar identically.
struct Summary {
    bool               has_sample_nav     = false;
    double             sample_lat         = 0.0;
    double             sample_lon         = 0.0;
    double             sample_alt         = 0.0;
    bool               sample_is_proj     = false;
    double             track_m            = 0.0;
    unsigned long long total_ssc_entries  = 0;
    unsigned long long preview_port_count = 0;
    bool               quality_reduced    = false;
};

// Path of the cache file for a given store + layer + quality tier.
std::string cachePath(const std::string& store_path,
                      const std::string& layer_id,
                      MapSonarQuality    quality);

// Build the freshness key from the live model state. `display_crs_id` is the id of
// the project's display CRS — the raster stores coordinates already reprojected to
// it, so a display-CRS change must invalidate the cache.
Meta makeMeta(const std::string&         store_path,
              const NavProcessingParams& nav,
              const SssGeorefParams&     georef,
              MapSonarQuality            quality,
              const std::string&         display_crs_id,
              const WaterfallParams&     sss_params = {});

// Cheap freshness probe: true if a cache file exists at `path` and its Meta matches
// `expect` (same store fingerprint, params, quality) — reads only the header+meta, not
// the raster body. Lets a loader decide to use a cached tier WITHOUT decoding pings.
bool isFresh(const std::string& path, const Meta& expect);

// Write the raster cache (atomic: temp file + rename). Returns false on I/O error.
bool save(const std::string&  path,
          const Meta&         meta,
          const Summary&      summary,
          const LayerMapData& data);

// Load a fresh raster cache. Returns false (leaving out/summary untouched) if the
// file is missing, corrupt, a version mismatch, or its Meta != `expect` (stale).
bool load(const std::string& path,
          const Meta&        expect,
          LayerMapData&      out,
          Summary&           summary);

} // namespace dolphin::ui::rastercache
