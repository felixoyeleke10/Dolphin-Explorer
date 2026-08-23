#pragma once
// SssMapBuild.h — background-safe swath geometry and preview image builders.
//
// Coverage path (CoverageOnly+):
//   buildSwathCoverage  — nav + slant range → ribbon polygons (no amplitude)
//   buildSwathNavTrack  — nav positions → track polyline + padded bbox
//   Both are O(n_pings), thread-safe, no Qt GUI calls.
//
// Preview image path (Low / Medium / High / Full):
//   buildSwathPreviewImage — georef + bilinear rasterize → QImage stored in ld
//   Cost is proportional to pings × samples × image pixels. Also thread-safe.
//   Fidelity is controlled by max_image_dim; the ping/sample counts are already
//   baked into the supplied ping vector by the caller.

#include "ui/features/map/MapTypes.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include "core/SidescanPing.h"
#include <atomic>
#include <functional>
#include <vector>

namespace dolphin::ui {

// Build port and starboard swath coverage ribbon polygons from normalized pings.
// Reads ld.is_projected (must be set first) to pick the segment-gap threshold.
// Uses the same SssGeorefParams as georeferenceSidescanPings so that coverage
// ribbons and the raster mosaic share identical heading data.
// Fills ld.coverage; existing coverage is cleared.
void buildSwathCoverage(const std::vector<core::SidescanPing>& pings,
                        LayerMapData& ld,
                        const SssGeorefParams& params = {},
                        std::vector<SwathCoverage>* nadir_hidden = nullptr);

// Build corrected sonar track + bounding box from pings. Uses the same
// georeference parameters as coverage/raster so placement cannot be cropped by
// a raw-vs-corrected nav mismatch. Pads bbox by max slant range.
// Returns the number of unique valid positions added.
size_t buildSwathNavTrack(const std::vector<core::SidescanPing>& pings,
                          LayerMapData& ld,
                          const SssGeorefParams& params = {});

// Build a raster sonar preview image from georeferenced pings.
// Prerequisites: ld.lon_min/max/lat_min/max must be valid (call buildSwathNavTrack first).
// max_image_dim:    maximum image dimension in pixels.
// palette_index:    from PaletteIndex:: constants.
// cancelled:        cooperative cancel flag checked between strips.
// min_strip_cos:    heading-change guard — cosine of the max allowed angle between
//                   consecutive strip cross-track directions; 0.0 = disabled.
// cell_budget_div:  pixel cap per cell = min(img_w,img_h) / div; 0 = no cap.
// Returns true and stores the image in ld.preview_image on success.
// ping_lines_only: debug flag — skip quad stitching; render each strip as
//   individual sample dots. Use to isolate whether stretching is a stitch bug.
// progress: optional 0.0–1.0 callback invoked as rasterization advances (georef →
//   stitch → pixel accounting). Called from this (background) thread; keep it lightweight
//   and thread-safe. Lets a long High/Full raster report sub-progress instead of
//   appearing frozen at one percentage.
bool buildSwathPreviewImage(const std::vector<core::SidescanPing>& pings,
                            LayerMapData& ld,
                            int max_image_dim,
                            int palette_index,
                            const std::atomic_bool& cancelled,
                            const SssGeorefParams& georef_params = {},
                            double min_strip_cos    = 0.940,
                            int    cell_budget_div  = 16,
                            bool   ping_lines_only  = false,
                            const std::function<void(float)>& progress = {},
                            float canonical_stretch_low  = -1.f,
                            float canonical_stretch_high = -1.f,
                            bool  produce_color_image = true);

} // namespace dolphin::ui
