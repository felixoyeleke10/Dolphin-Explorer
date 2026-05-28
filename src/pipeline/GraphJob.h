#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace dolphin::pipeline {

// Per-node result captured during one graph execution.
struct NodeResult {
    std::string node_id;
    std::string node_type;
    bool        cache_hit    = false;
    bool        failed       = false;
    std::string error;
    int64_t     duration_us  = 0;   // wall time spent in process()
    size_t      input_count  = 0;   // artifacts received
    size_t      output_count = 0;   // artifacts emitted
};

// Complete record of one graph execution pass on one survey line.
// Designed to be serializable so runs can be logged and diffed.
struct GraphJob {
    std::string             line_id;
    std::string             source_path;
    int64_t                 started_at_us = 0;
    int64_t                 ended_at_us   = 0;
    bool                    completed     = false;
    size_t                  total_input   = 0;  // artifacts in source buffer
    size_t                  total_output  = 0;  // artifacts in final buffer
    std::vector<NodeResult> node_results;

    int64_t durationUs() const { return ended_at_us - started_at_us; }

    bool anyFailed() const {
        for (auto& r : node_results)
            if (r.failed) return true;
        return false;
    }

    int cacheHits() const {
        int n = 0;
        for (auto& r : node_results)
            if (r.cache_hit) ++n;
        return n;
    }

    // One-line summary for status bars and logs
    std::string summary() const;

    // Full JSON record for persistent job logs
    std::string toJson() const;
};

} // namespace dolphin::pipeline
