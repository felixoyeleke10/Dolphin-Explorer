#pragma once
#include "core/Artifact.h"
#include "core/SpatialRef.h"
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace dolphin::core {

// One seek-table entry for a single artifact in a raw file.
// Built once on import; enables O(1) random access without re-scanning.
struct ArtifactIndexEntry {
    uint64_t     artifact_id  = 0;
    ArtifactType type         = ArtifactType::Sidescan;
    int64_t      timestamp_us = 0;
    uint64_t     file_offset  = 0;   // byte offset of the containing record
    uint32_t     subrecord_offset = 0; // offset from file_offset to a channel header
    uint32_t     byte_length  = 0;   // bytes in this record or subrecord
    double       lat          = 0.0;
    double       lon          = 0.0;
    uint32_t     ping_number  = 0;   // XTF PingNumber; 0 = unknown (JSF, cache-built indices)
    float        frequency_hz = 0.f; // centre frequency of this ping; 0 = unknown
    SpatialRefKind spatial_ref_kind = SpatialRefKind::Unknown;
    bool         is_projected = false; // true when lat/lon are projected metres (UTM etc.), not WGS84
};

struct ArtifactIndex {
    std::string                     source_id;
    std::vector<ArtifactIndexEntry> entries;

    size_t size()  const { return entries.size(); }
    bool   empty() const { return entries.empty(); }

    // Returns -1 if not found
    int64_t findById(uint64_t artifact_id) const {
        for (int64_t i = 0; i < static_cast<int64_t>(entries.size()); ++i)
            if (entries[i].artifact_id == artifact_id) return i;
        return -1;
    }

    int64_t findNearestTimestamp(int64_t ts) const {
        if (entries.empty()) return -1;
        auto it = std::lower_bound(entries.begin(), entries.end(), ts,
            [](const ArtifactIndexEntry& e, int64_t t) {
                return e.timestamp_us < t;
            });
        if (it == entries.end())
            return static_cast<int64_t>(entries.size() - 1);
        if (it == entries.begin())
            return 0;
        auto prev = std::prev(it);
        if (std::abs(prev->timestamp_us - ts) < std::abs(it->timestamp_us - ts))
            return static_cast<int64_t>(std::distance(entries.begin(), prev));
        return static_cast<int64_t>(std::distance(entries.begin(), it));
    }

    // Bounding box of nav positions stored in the index (O(n) in-memory, no I/O).
    // Used to pre-position the map viewport before the background ping load starts.
    struct NavExtent {
        double lat_min =  1e18, lat_max = -1e18;
        double lon_min =  1e18, lon_max = -1e18;
        bool   valid   = false;
    };
    NavExtent navExtent() const {
        NavExtent e;
        for (const auto& en : entries) {
            if (en.lat == 0.0 && en.lon == 0.0) continue;
            if (en.lat < e.lat_min) e.lat_min = en.lat;
            if (en.lat > e.lat_max) e.lat_max = en.lat;
            if (en.lon < e.lon_min) e.lon_min = en.lon;
            if (en.lon > e.lon_max) e.lon_max = en.lon;
            e.valid = true;
        }
        return e;
    }

    // Return pointers to all entries of a given type (O(n) scan)
    std::vector<const ArtifactIndexEntry*> byType(ArtifactType t) const {
        std::vector<const ArtifactIndexEntry*> result;
        for (auto& e : entries)
            if (e.type == t) result.push_back(&e);
        return result;
    }
};

} // namespace dolphin::core
