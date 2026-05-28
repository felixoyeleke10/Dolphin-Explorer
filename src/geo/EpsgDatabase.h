#pragma once
#include "core/SpatialRef.h"
#include <string>
#include <vector>

namespace dolphin::geo {

struct EpsgEntry {
    int                  code     = 0;
    std::string          short_name;  // "UTM Zone 30N"
    std::string          full_name;   // "WGS 84 / UTM Zone 30N"
    core::SpatialRefKind kind     = core::SpatialRefKind::Projected;
    bool                 is_common = false;
};

// Full curated list of survey-relevant CRS.
// Ordering: common entries first, then alphabetical by full_name.
const std::vector<EpsgEntry>& epsgDatabase();

// Look up by integer code. Returns nullptr if not found.
const EpsgEntry* epsgByCode(int code);

// Look up by SpatialRef. Parses ref.id for "EPSG:NNNN" or "WGS84".
const EpsgEntry* epsgByRef(const core::SpatialRef& ref);

// Human-readable display name for UI labels.
//   Known EPSG  → "WGS 84 / UTM Zone 30N  ·  EPSG:32630"
//   WGS84 alias → "WGS 84  ·  EPSG:4326"
//   Auto-detect → "Projected — CRS not set"
//   Exact user  → "EPSG:27700"  (unknown code the user typed)
std::string epsgDisplayName(const core::SpatialRef& ref);

// Returns true when ref.id is an internal auto-detection tag
// (PROJECTED:XTF_MAGNITUDE, PROJECTED:UNKNOWN, etc.) that means
// the real CRS is not yet confirmed by the user.
bool epsgIsUnconfirmed(const core::SpatialRef& ref);

} // namespace dolphin::geo
