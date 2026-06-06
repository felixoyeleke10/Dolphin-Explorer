#include "app/corrections/SidescanCorrectionService.h"
#include "app/corrections/CorrectionAlgorithms.h"
#include "app/services/ImportService.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "io/cache/ParsedCache.h"
#include "io/xtf/XtfReader.h"
#include "io/jsf/JsfReader.h"
#include "core/Artifact.h"
#include "core/SidescanPing.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <cctype>

namespace dolphin::app {

namespace {

using namespace dolphin;

std::string normaliseFormat(std::string fmt)
{
    for (auto& c : fmt) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return fmt;
}

std::unique_ptr<io::IFormatReader> makeReader(const std::string& format)
{
    const std::string fmt = normaliseFormat(format);
    if (fmt == "dlpd" || fmt == "dpcache") return std::make_unique<io::ParsedCacheReader>();
    if (fmt == "jsf")                       return std::make_unique<io::JsfReader>();
    return std::make_unique<io::XtfReader>();
}


struct CorrectionRequest {
    std::string             layer_id;
    std::string             store_path;
    std::string             store_format;
    std::string             source_path;
    core::ArtifactIndex     artifact_index;
    SidescanCorrectionParams params;
};

struct CorrectionResult {
    std::string         layer_id;
    std::string         new_path;
    core::ArtifactIndex new_index;
    std::string         error;
    bool                ok      = false;
    bool                skipped = false;
};

CorrectionResult execute(const CorrectionRequest& req, ImportService* svc)
{
    CorrectionResult result;
    result.layer_id = req.layer_id;

    const std::string fmt = normaliseFormat(req.store_format);
    if (fmt != "dlpd" && fmt != "dpcache") {
        result.error = "Corrections can only be baked into .dlpd stores (layer: "
                     + req.layer_id + ")";
        return result;
    }

    io::FormatMeta meta;
    {
        auto reader = makeReader(fmt);
        if (!reader->open(req.store_path)) {
            result.error = "Cannot open artifact store: " + req.store_path;
            return result;
        }
        meta = reader->metadata();
    }

    auto pings = svc->loadAllSidescanPingsFromStore(
        req.store_path, req.store_format, req.artifact_index, req.source_path);

    if (pings.empty()) {
        result.error = "No sidescan pings loaded from " + req.store_path;
        return result;
    }

    const uint32_t already_baked = pings.front().correction_flags;
    bool modified = false;

    if (req.params.tvg.enabled &&
        !core::hasCorrectionFlag(already_baked, core::CorrectionFlag::Tvg)) {
        corrections::applyTvg(pings, req.params.tvg);
        for (auto& p : pings) p.correction_flags |= core::CorrectionFlag::Tvg;
        modified = true;
    }

    if (req.params.arc.enabled &&
        !core::hasCorrectionFlag(already_baked, core::CorrectionFlag::Arc)) {
        corrections::applyArc(pings, req.params.arc);
        for (auto& p : pings) p.correction_flags |= core::CorrectionFlag::Arc;
        modified = true;
    }

    if (req.params.agc.enabled &&
        !core::hasCorrectionFlag(already_baked, core::CorrectionFlag::GainNormalized)) {
        corrections::normalizeAmplitudes(pings, req.params.agc);
        for (auto& p : pings) p.correction_flags |= core::CorrectionFlag::GainNormalized;
        modified = true;
    }

    if (!modified) {
        result.ok      = true;
        result.skipped = true;
        return result;
    }

    std::vector<core::Artifact> buffer;
    buffer.reserve(pings.size());
    for (auto& ping : pings)
        buffer.emplace_back(std::move(ping));

    core::ArtifactIndex out_index;
    if (!io::writeArtifactBufferToCache(req.store_path, buffer, meta, out_index)) {
        result.error = "Failed to write corrected data to " + req.store_path;
        return result;
    }

    out_index.source_id = req.artifact_index.source_id;
    result.ok       = true;
    result.new_path = req.store_path;
    result.new_index = std::move(out_index);
    return result;
}

} // namespace

SidescanCorrectionService::SidescanCorrectionService(ImportService* import_service,
                                                     QObject* parent)
    : QObject(parent)
    , m_import_service(import_service)
{}

void SidescanCorrectionService::applyToLine(
    const std::string& layer_id,
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path,
    const SidescanCorrectionParams& params)
{
    CorrectionRequest req;
    req.layer_id       = layer_id;
    req.store_path     = store_path;
    req.store_format   = store_format;
    req.source_path    = source_path;
    req.artifact_index = artifact_index;
    req.params         = params;

    emit applyStarted(layer_id);

    ImportService* svc = m_import_service;
    auto* watcher = new QFutureWatcher<CorrectionResult>(this);
    connect(watcher, &QFutureWatcher<CorrectionResult>::finished, this,
            [this, watcher]() {
                watcher->deleteLater();
                CorrectionResult res;
                try { res = watcher->result(); } catch (...) { return; }
                if (res.skipped) return;
                if (!res.ok)
                    emit applyFailed(res.layer_id, res.error);
                else
                    emit correctionsPersisted(res.layer_id, res.new_path, res.new_index);
            });
    watcher->setFuture(QtConcurrent::run([req, svc]() {
        return execute(req, svc);
    }));
}

void SidescanCorrectionService::applyToAll(Project& project,
                                            const SidescanCorrectionParams& params)
{
    for (const auto& layer : project.layers()) {
        if (!layer || layer->modality != Modality::Sidescan) continue;
        if (!layer->index_built || layer->sidescanCount() == 0) continue;
        const auto* src = project.findSource(layer->source_id);
        applyToLine(layer->id,
                    layer->artifact_store_path,
                    layer->artifact_store_format,
                    layer->artifact_index,
                    src ? src->path : std::string{},
                    params);
    }
}

} // namespace dolphin::app
