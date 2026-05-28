#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

namespace dolphin::io {

// One GPS/nav fix scraped from an XTF file.
struct XtfNavFix {
    int64_t timestamp_us = 0;
    double  lat          = 0.0;
    double  lon          = 0.0;
};

// Lazily loads nav fixes from an open XTF file and provides linear interpolation.
// Designed to be embedded as a value member in XtfReader.
struct XtfNavTable {
    mutable std::vector<XtfNavFix> fixes;
    mutable bool                   loaded = false;

    // Scan the file (from data_start to file_size) and populate fixes.
    // Saves/restores the file position so callers' seek state is unaffected.
    void load(FILE* file, uint64_t data_start, uint64_t file_size) const;

    // Set out_lat/out_lon to the interpolated position at timestamp_us.
    // No-op when fixes is empty.
    void interpolate(int64_t timestamp_us,
                     double& out_lat, double& out_lon) const;

    bool empty() const { return fixes.empty(); }
    void reset() { fixes.clear(); loaded = false; }
};

} // namespace dolphin::io
