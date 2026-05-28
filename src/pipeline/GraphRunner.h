#pragma once
#include "pipeline/NodeGraph.h"
#include "pipeline/GraphJob.h"
#include "core/ArtifactIndex.h"
#include "io/IFormatReader.h"
#include <functional>

namespace dolphin::pipeline {

struct GraphRunnerOptions {
    bool verbose     = false;  // emit timing lines to stderr
    bool force_rerun = false;  // bypass all cached results
};

// Headless graph execution engine.
//
// No Qt widgets are created here. GraphRunner can be called from a CLI binary
// (QCoreApplication), a test harness, or a background thread. The UI merely
// calls GraphRunner and reads back the GraphJob result.
//
// Execution model:
//   source artifacts  →  [node₀ → node₁ → … → nodeN]  →  processed artifacts
//
// Every call is deterministic: same graph + same source = same output.
// Cache hits are detected inside NodeGraph and recorded in the GraphJob.
class GraphRunner {
public:
    using ProgressFn = std::function<void(size_t done, size_t total,
                                          const GraphJob& last)>;

    // Core executor — takes a pre-loaded artifact buffer.
    // Populates `job` with per-node timing, cache hits, and errors.
    static ArtifactBuffer run(NodeGraph&              graph,
                               const ArtifactBuffer&  source,
                               GraphJob&              job,
                               GraphRunnerOptions      opts = GraphRunnerOptions{});

    // Convenience: load all artifacts from an index + open reader, then run.
    // `reader` must already be open on the correct file.
    static ArtifactBuffer runLine(NodeGraph&                   graph,
                                   const core::ArtifactIndex&  index,
                                   io::IFormatReader&           reader,
                                   GraphJob&                    job,
                                   GraphRunnerOptions           opts = GraphRunnerOptions{});

    // Apply one graph to a set of lines (batch mode).
    // Returns one GraphJob per line. `progress` is called after each line.
    static std::vector<GraphJob>
        runBatch(NodeGraph&                                    graph,
                 const std::vector<core::ArtifactIndex*>&     indices,
                 const std::vector<io::IFormatReader*>&        readers,
                 GraphRunnerOptions                            opts = GraphRunnerOptions{},
                 ProgressFn                                    progress = ProgressFn{});
};

} // namespace dolphin::pipeline
