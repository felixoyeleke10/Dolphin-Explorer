#include "pipeline/GraphRunner.h"
#include <chrono>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <ctime>
#endif

namespace dolphin::pipeline {

static int64_t nowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// ── GraphJob helpers ──────────────────────────────────────────────────────────

std::string GraphJob::summary() const
{
    std::ostringstream ss;
    ss << (completed ? "OK" : "FAILED")
       << "  nodes=" << node_results.size()
       << "  cache_hits=" << cacheHits()
       << "  in=" << total_input
       << "  out=" << total_output
       << "  " << (durationUs() / 1000) << " ms";
    return ss.str();
}

std::string GraphJob::toJson() const
{
    auto escape = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            if      (c == '\\') out += "\\\\";
            else if (c == '"')  out += "\\\"";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        return out;
    };

    std::ostringstream ss;
    ss << "{\n"
       << "  \"line_id\": \""     << escape(line_id)     << "\",\n"
       << "  \"source_path\": \"" << escape(source_path) << "\",\n"
       << "  \"started_at_us\": " << started_at_us << ",\n"
       << "  \"ended_at_us\": "   << ended_at_us   << ",\n"
       << "  \"completed\": "     << (completed ? "true" : "false") << ",\n"
       << "  \"total_input\": "   << total_input  << ",\n"
       << "  \"total_output\": "  << total_output << ",\n"
       << "  \"nodes\": [\n";

    for (size_t i = 0; i < node_results.size(); ++i) {
        auto& r = node_results[i];
        ss << "    {"
           << "\"id\": \""   << escape(r.node_id)   << "\", "
           << "\"type\": \"" << escape(r.node_type) << "\", "
           << "\"cache_hit\": " << (r.cache_hit ? "true" : "false") << ", "
           << "\"failed\": "    << (r.failed    ? "true" : "false") << ", "
           << "\"duration_us\": " << r.duration_us << ", "
           << "\"in\": "  << r.input_count  << ", "
           << "\"out\": " << r.output_count;
        if (!r.error.empty())
            ss << ", \"error\": \"" << escape(r.error) << "\"";
        ss << "}";
        if (i + 1 < node_results.size()) ss << ",";
        ss << "\n";
    }

    ss << "  ]\n}\n";
    return ss.str();
}

// ── Core executor ─────────────────────────────────────────────────────────────

ArtifactBuffer GraphRunner::run(NodeGraph&            graph,
                                 const ArtifactBuffer& source,
                                 GraphJob&             job,
                                 GraphRunnerOptions    opts)
{
    job.started_at_us = nowUs();
    job.total_input   = source.size();
    job.node_results.clear();

    if (opts.force_rerun)
        graph.markAllDirty();

    // Execute the graph — NodeGraph handles topo sort, caching, and node dispatch.
    // We wrap each node's execution to capture timing and errors.
    // To do this cleanly we override execute() with a job-aware version below.
    ArtifactBuffer result = graph.execute(source, job);

    job.total_output  = result.size();
    job.ended_at_us   = nowUs();
    job.completed     = !job.anyFailed();

    return result;
}

// ── Line runner ───────────────────────────────────────────────────────────────

ArtifactBuffer GraphRunner::runLine(NodeGraph&                  graph,
                                     const core::ArtifactIndex& index,
                                     io::IFormatReader&          reader,
                                     GraphJob&                   job,
                                     GraphRunnerOptions          opts)
{
    // Load all artifacts from the index
    ArtifactBuffer source;
    source.reserve(index.size());

    for (auto& entry : index.entries) {
        auto artifact = reader.readArtifact(entry);
        if (artifact) source.push_back(std::move(*artifact));
    }

    return run(graph, source, job, opts);
}

// ── Batch runner ──────────────────────────────────────────────────────────────

std::vector<GraphJob>
GraphRunner::runBatch(NodeGraph&                                   graph,
                       const std::vector<core::ArtifactIndex*>&    indices,
                       const std::vector<io::IFormatReader*>&      readers,
                       GraphRunnerOptions                           opts,
                       GraphRunner::ProgressFn                      progress)
{
    std::vector<GraphJob> jobs;
    jobs.reserve(indices.size());

    size_t total = indices.size();
    for (size_t i = 0; i < total; ++i) {
        GraphJob job;
        // Each line runs with a fresh dirty state so results don't bleed across
        graph.markAllDirty();

        ArtifactBuffer result = runLine(graph, *indices[i], *readers[i], job, opts);
        jobs.push_back(std::move(job));

        if (progress) progress(i + 1, total, jobs.back());
    }

    return jobs;
}

} // namespace dolphin::pipeline
