#include "app/services/ProcessingService.h"
#include "app/layers/DataLayer.h"
#include "core/Artifact.h"
#include "io/jsf/JsfReader.h"
#include "io/cache/ParsedCache.h"
#include "io/xtf/XtfReader.h"
#include "pipeline/GraphRunner.h"
#include "pipeline/NodeGraph.h"
#include <QFutureWatcher>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
#include <cctype>
#include <filesystem>
#include <memory>
#include <set>

namespace {

using namespace dolphin;

struct RunRequest {
    std::string         layer_id;
    std::string         source_path;
    std::string         artifact_path;
    std::string         artifact_format;
    std::string         graph_json;
    core::ArtifactIndex artifact_index;
};

struct RunResult {
    std::string         layer_id;
    std::string         summary;
    std::string         error;
    bool                failed                = false;
    bool                slant_range_corrected = false;
    std::string         processed_path;   // non-empty when cache was written successfully
    core::ArtifactIndex processed_index;
};

struct BatchResult {
    std::vector<RunResult> runs;
    int                    succeeded = 0;
    int                    total     = 0;
};

std::string normaliseFormat(std::string format)
{
    for (auto& c : format)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return format;
}

std::string formatFromPath(const std::string& path, const std::string& fallback = "xtf")
{
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return fallback;
    return normaliseFormat(path.substr(dot + 1));
}

std::unique_ptr<io::IFormatReader> makeReader(const std::string& format)
{
    const std::string fmt = normaliseFormat(format);
    if (fmt == "dlpd" || fmt == "dpcache")
        return std::make_unique<io::ParsedCacheReader>();
    if (fmt == "jsf")
        return std::make_unique<io::JsfReader>();
    return std::make_unique<io::XtfReader>();
}

RunResult executeRequest(const RunRequest& request)
{
    RunResult result;
    result.layer_id = request.layer_id;

    pipeline::NodeGraph graph;
    if (!graph.fromJson(request.graph_json)) {
        result.failed = true;
        result.error  = "Could not restore the node graph for processing";
        return result;
    }

    auto reader = makeReader(request.artifact_format);
    if (!reader->open(request.artifact_path)) {
        result.failed = true;
        result.error  = "Cannot open artifact store: " + request.artifact_path;
        return result;
    }

    if (graph.nodes().empty()) {
        result.failed = true;
        result.error  = "Processing graph has no nodes";
        return result;
    }

    // Capture metadata before the reader is consumed by runLine (non-const: we set
    // the role marker on the output below).
    io::FormatMeta meta = reader->metadata();

    // Always write to a per-layer sidecar — never overwrite the original parsed
    // store (D-04), even for an explicit processing run. A store is "already our
    // sidecar" when its formal role marker says so (preferred), or — for stores
    // written before the marker existed — when the filename carries the legacy
    // "_<layerId>" suffix; re-runs overwrite that sidecar in place.
    std::string write_path;
    {
        namespace fs = std::filesystem;
        const fs::path p(request.artifact_path);
        const std::string suffix = "_" + request.layer_id;
        const std::string stem   = p.stem().string();
        const bool legacy_named = stem.size() > suffix.size()
            && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0;
        const bool already_sidecar =
            (meta.artifact_role == io::kArtifactRoleSidecar) || legacy_named;
        write_path = already_sidecar
            ? request.artifact_path
            : (p.parent_path() / (stem + suffix + ".dlpd")).string();
    }
    meta.artifact_role = io::kArtifactRoleSidecar;  // formal marker on the output

    pipeline::GraphJob job;
    job.line_id     = request.layer_id;
    job.source_path = request.source_path;

    // Run the graph and capture the corrected artifact buffer.
    auto processed = pipeline::GraphRunner::runLine(
        graph, request.artifact_index, *reader, job);

    if (job.anyFailed()) {
        result.failed = true;
        result.error  = "One or more nodes failed - " + job.summary();
        return result;
    }

    result.summary = job.summary();

    // Detect whether SlantRangeNode ran — check if any SSS ping in the
    // processed buffer has the SlantRange correction flag set.
    for (const auto& artifact : processed) {
        if (const auto* ping = std::get_if<core::SidescanPing>(&artifact)) {
            if (hasCorrectionFlag(ping->correction_flags, core::CorrectionFlag::SlantRange)) {
                result.slant_range_corrected = true;
                break;
            }
        }
    }

    // Overwrite the existing .dlpd cache in-place (atomic temp+rename).
    // The original raw source (XTF/JSF) is untouched — re-indexing always
    // produces a clean baseline from the raw file.
    if (!processed.empty()) {
        reader.reset(); // release the read handle before writing to the same path

        core::ArtifactIndex proc_index;
        if (io::writeArtifactBufferToCache(write_path, processed, meta, proc_index)) {
            proc_index.source_id   = request.artifact_index.source_id; // restore after write zeroes it
            result.processed_path  = write_path;
            result.processed_index = std::move(proc_index);
        } else {
            result.failed = true;
            result.error  = "Failed to write processed data to " + write_path;
        }
    }

    return result;
}

} // namespace

namespace dolphin::app {

ProcessingService::ProcessingService(QObject* parent) : QObject(parent) {}

ProcessingService::~ProcessingService()
{
    // Wait for any in-flight background futures before destruction so they
    // cannot access members of a partially-destroyed object.
    for (auto* child : children()) {
        if (auto* w = qobject_cast<QFutureWatcherBase*>(child))
            w->waitForFinished();
    }
}

void ProcessingService::runLayer(Project& project, DataLayer* layer, const std::string& source_path)
{
    if (!layer || !layer->index_built) return;

    RunRequest request;
    request.layer_id       = layer->id;
    request.source_path    = source_path;
    if (layer->artifact_store_path.empty()) {
        emit runFailed(layer->id,
                       "Layer has no indexed artifact — re-import the file before running processing");
        return;
    }
    request.artifact_path   = layer->artifact_store_path;
    request.artifact_format = layer->artifact_store_format.empty()
        ? formatFromPath(request.artifact_path)
        : normaliseFormat(layer->artifact_store_format);
    request.graph_json     = layer->uses_project_graph
        ? project.processing_graph.toJson()
        : layer->node_graph.toJson();
    request.artifact_index = layer->artifact_index;

    if (!m_active_paths.insert(request.artifact_path).second) {
        emit runFailed(request.layer_id,
                       "Another run is already writing to " + request.artifact_path);
        return;
    }

    emit runStarted(request.layer_id);

    const std::string artifact_path = request.artifact_path;
    auto* watcher = new QFutureWatcher<RunResult>(this);
    connect(watcher, &QFutureWatcher<RunResult>::finished, this,
            [this, watcher, artifact_path]() {
        RunResult result = watcher->result();
        watcher->deleteLater();
        m_active_paths.erase(artifact_path);

        if (result.failed) {
            emit runFailed(result.layer_id, result.error);
        } else {
            emit runComplete(result.layer_id, result.summary);
            if (!result.processed_path.empty())
                emit runPersisted(result.layer_id,
                                  result.processed_path,
                                  result.processed_index,
                                  result.slant_range_corrected);
        }
    });
    watcher->setFuture(QtConcurrent::run([request]() {
        return executeRequest(request);
    }));
}

void ProcessingService::runAll(Project& project)
{
    std::vector<RunRequest> requests;
    requests.reserve(project.layers().size());

    for (auto& layer : project.layers()) {
        if (!layer || !layer->index_built) continue;

        auto* src = project.findSource(layer->source_id);
        if (!src) continue;

        // Skip layers that have no indexed artifact — never fall back to the raw source.
        if (layer->artifact_store_path.empty()) continue;

        // Skip if an in-flight runLayer call is already writing to this store.
        // (Layers sharing a store each write to their own per-layer sidecar, so
        // there is no deduplication by store path needed — every layer runs.)
        const std::string store_path = layer->artifact_store_path;
        if (m_active_paths.count(store_path))
            continue;

        RunRequest request;
        request.layer_id        = layer->id;
        request.source_path     = src->path;
        request.artifact_path   = store_path;
        request.artifact_format = layer->artifact_store_format.empty()
            ? formatFromPath(request.artifact_path)
            : normaliseFormat(layer->artifact_store_format);
        request.graph_json      = layer->uses_project_graph
            ? project.processing_graph.toJson()
            : layer->node_graph.toJson();
        request.artifact_index  = layer->artifact_index;
        requests.push_back(std::move(request));
    }

    if (requests.empty()) {
        emit batchComplete(0, 0);
        return;
    }

    for (const auto& req : requests)
        emit runStarted(req.layer_id);

    emit batchProgress(0, static_cast<int>(requests.size()));

    // self (QPointer) is used as the invokeMethod target so Qt will discard
    // any queued calls if the object is destroyed before they run on the
    // main thread.  ~ProcessingService() calls waitForFinished() to ensure
    // the background loop has finished posting before the object is torn down.
    QPointer<ProcessingService> self(this);
    auto* watcher = new QFutureWatcher<BatchResult>(this);
    connect(watcher, &QFutureWatcher<BatchResult>::finished, this, [this, watcher]() {
        BatchResult batch = watcher->result();
        watcher->deleteLater();

        for (const auto& run : batch.runs) {
            if (run.failed) {
                emit runFailed(run.layer_id, run.error);
            } else {
                emit runComplete(run.layer_id, run.summary);
                if (!run.processed_path.empty())
                    emit runPersisted(run.layer_id,
                                      run.processed_path,
                                      run.processed_index,
                                      run.slant_range_corrected);
            }
        }

        emit batchComplete(batch.succeeded, batch.total);
    });
    watcher->setFuture(QtConcurrent::run([requests, self]() {
        BatchResult batch;
        batch.total = static_cast<int>(requests.size());

        for (size_t i = 0; i < requests.size(); ++i) {
            RunResult run = executeRequest(requests[i]);
            if (!run.failed) ++batch.succeeded;
            batch.runs.push_back(std::move(run));

            int done  = static_cast<int>(i + 1);
            int total = batch.total;
            QMetaObject::invokeMethod(self.data(), [self, done, total]() {
                if (self) Q_EMIT self->batchProgress(done, total);
            }, Qt::QueuedConnection);
        }

        return batch;
    }));
}

} // namespace dolphin::app
