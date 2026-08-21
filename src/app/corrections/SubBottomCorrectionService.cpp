#include "app/corrections/SubBottomCorrectionService.h"
#include "app/artifacts/ArtifactSidecar.h"
#include "app/corrections/SubBottomCorrectionAlgorithms.h"
#include "app/contracts/ProcessingSettingsContract.h"
#include "app/services/ImportService.h"
#include "io/cache/ParsedCache.h"
#include "core/Artifact.h"
#include "core/SubBottomTrace.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace dolphin::app {

namespace {

using namespace dolphin;

std::string normaliseFormat(std::string fmt)
{
    for (auto& c : fmt) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return fmt;
}

struct SbpCorrectionRequest {
    std::string          layer_id;
    std::string          store_path;
    std::string          store_format;
    std::string          source_path;
    core::ArtifactIndex  artifact_index;
    SbpGainParams        gain;
    SbpSignalParams      signal;
};

struct SbpCorrectionResult {
    std::string         layer_id;
    std::string         new_path;
    core::ArtifactIndex new_index;
    std::string         error;
    bool                ok      = false;
    bool                skipped = false;
    uint32_t            baked_correction_flags = 0;
};

SbpCorrectionResult execute(const SbpCorrectionRequest& req)
{
    SbpCorrectionResult result;
    result.layer_id = req.layer_id;
    if (const std::string error = contracts::validate(req.gain, req.signal);
        !error.empty()) {
        result.error = "Invalid sub-bottom correction settings: " + error;
        return result;
    }

    const std::string fmt = normaliseFormat(req.store_format);
    if (fmt != "dlpd" && fmt != "dpcache") {
        result.error = "Corrections can only be baked into .dlpd stores (layer: "
                     + req.layer_id + ")";
        return result;
    }

    io::FormatMeta meta;
    std::string write_path = req.store_path;
    {
        io::ParsedCacheReader reader;
        if (!reader.open(req.store_path)) {
            result.error = "Cannot open artifact store: " + req.store_path;
            return result;
        }
        meta = reader.metadata();
        // Always write corrections to a per-layer sidecar — never overwrite the
        // original parsed store (D-04: the imported .dlpd is a durable asset; a
        // full-store Apply must not replace it). "Already our sidecar" = formal role
        // marker (preferred) or the legacy "_<layerId>" filename suffix; re-applies
        // overwrite that sidecar in place rather than nesting another suffix.
        write_path = dolphin::app::sidecarArtifactPath(
            req.store_path, req.layer_id, meta.artifact_role);
        meta.artifact_role = io::kArtifactRoleSidecar;  // formal marker on the output
    }

    auto traces = ImportService::loadAllSubBottomTraces(
        req.store_path, req.store_format, req.artifact_index, req.source_path);

    if (traces.empty()) {
        result.error = "No sub-bottom traces loaded from " + req.store_path;
        return result;
    }

    uint32_t flags_before = 0;
    for (const auto& trace : traces) flags_before |= trace.correction_flags;
    corrections::applySubBottomCorrections(traces, req.gain, req.signal);
    uint32_t flags_after = 0;
    for (const auto& trace : traces) flags_after |= trace.correction_flags;
    const bool modified = flags_after != flags_before;

    if (!modified) {
        result.ok        = true;
        result.skipped   = true;
        result.new_path  = req.store_path;
        result.new_index = req.artifact_index;
        return result;
    }

    std::vector<core::Artifact> buffer;
    buffer.reserve(traces.size());
    for (auto& t : traces) {
        result.baked_correction_flags |= t.correction_flags;
        buffer.emplace_back(std::move(t));
    }

    core::ArtifactIndex out_index;
    if (!io::writeArtifactBufferToCache(write_path, buffer, meta, out_index)) {
        result.error = "Failed to write corrected data to " + write_path;
        return result;
    }

    out_index.source_id = req.artifact_index.source_id;
    result.ok        = true;
    result.new_path  = write_path;
    result.new_index = std::move(out_index);
    return result;
}

} // namespace

SubBottomCorrectionService::SubBottomCorrectionService(QObject* parent)
    : QObject(parent)
{}

void SubBottomCorrectionService::applyToLine(
    const std::string& layer_id,
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path,
    const SbpGainParams& gain,
    const SbpSignalParams& signal)
{
    SbpCorrectionRequest req;
    req.layer_id       = layer_id;
    req.store_path     = store_path;
    req.store_format   = store_format;
    req.source_path    = source_path;
    req.artifact_index = artifact_index;
    req.gain           = gain;
    req.signal         = signal;

    auto* watcher = new QFutureWatcher<SbpCorrectionResult>(this);
    connect(watcher, &QFutureWatcher<SbpCorrectionResult>::finished, this,
            [this, watcher, layer_id]() {
                watcher->deleteLater();
                SbpCorrectionResult res;
                try { res = watcher->result(); } catch (...) {
                    emit applyFailed(layer_id, "Unexpected exception in SBP correction thread");
                    return;
                }
                if (!res.ok) {
                    emit applyFailed(res.layer_id, res.error);
                } else if (res.skipped) {
                    emit applySkipped(res.layer_id);
                } else {
                    emit correctionsPersisted(res.layer_id, res.new_path, res.new_index,
                                              res.baked_correction_flags);
                }
            });
    watcher->setFuture(QtConcurrent::run([req]() {
        return execute(req);
    }));
}


} // namespace dolphin::app
