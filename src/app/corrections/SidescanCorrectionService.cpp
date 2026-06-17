#include "app/corrections/SidescanCorrectionService.h"
#include "app/corrections/CorrectionAlgorithms.h"
#include "app/services/ImportService.h"
#include "io/cache/ParsedCache.h"
#include "io/xtf/XtfReader.h"
#include "io/jsf/JsfReader.h"
#include "core/Artifact.h"
#include "core/SidescanPing.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <cctype>
#include <filesystem>
#include <unordered_map>

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
    // Optional: bottom picks from the viewer to merge into the DLPD in the same
    // write pass.  Matched by timestamp_us; source==0 picks are ignored.
    std::vector<core::SidescanPing> viewer_pings;
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
    std::string write_path = req.store_path;
    {
        auto reader = makeReader(fmt);
        if (!reader->open(req.store_path)) {
            result.error = "Cannot open artifact store: " + req.store_path;
            return result;
        }
        meta = reader->metadata();
        // Always write corrections to a per-layer sidecar — never overwrite the
        // original parsed store. The imported .dlpd is a durable project asset
        // (D-04), and a single full-store layer's Apply must not replace it (this
        // also protects sibling layers of a shared/dual-frequency store). A store is
        // "already our sidecar" when its formal role marker says so (preferred), or —
        // for stores written before the marker existed — when the filename carries the
        // legacy "_<layerId>" suffix. Re-applies overwrite that sidecar in place
        // rather than nesting another suffix.
        namespace fs = std::filesystem;
        const fs::path p(req.store_path);
        const std::string suffix = "_" + req.layer_id;
        const std::string stem   = p.stem().string();
        const bool legacy_named = stem.size() > suffix.size()
            && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0;
        const bool already_sidecar =
            (meta.artifact_role == io::kArtifactRoleSidecar) || legacy_named;
        write_path = already_sidecar
            ? req.store_path
            : (p.parent_path() / (stem + suffix + ".dlpd")).string();
        meta.artifact_role = io::kArtifactRoleSidecar;  // formal marker on the output
    }

    auto pings = svc->loadAllSidescanPingsFromStore(
        req.store_path, req.store_format, req.artifact_index, req.source_path);

    if (pings.empty()) {
        result.error = "No sidescan pings loaded from " + req.store_path;
        return result;
    }

    // AND of all ping flags: only skip a correction if every ping already has it.
    uint32_t already_baked = ~0u;
    for (const auto& p : pings) already_baked &= p.correction_flags;
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

    // Merge bottom picks from the viewer into the DLPD pings (same write pass).
    // Picks are matched by timestamp_us; only source>0 (detected/user-edited) picks
    // are applied so untracked pings keep their existing (or absent) picks.
    if (!req.viewer_pings.empty()) {
        std::unordered_map<uint64_t, const core::SidescanPing*> ts_map;
        for (const auto& vp : req.viewer_pings)
            if (vp.bottom_pick.source > 0 && vp.bottom_pick.range_m > 0.f)
                ts_map[vp.timestamp_us] = &vp;

        if (!ts_map.empty()) {
            for (auto& ping : pings) {
                const auto it = ts_map.find(ping.timestamp_us);
                if (it == ts_map.end()) continue;
                const auto& vbp = it->second->bottom_pick;
                if (ping.bottom_pick.source  != vbp.source  ||
                    ping.bottom_pick.range_m != vbp.range_m) {
                    ping.bottom_pick = vbp;
                    modified = true;
                }
            }
        }
    }

    if (!modified) {
        result.ok        = true;
        result.skipped   = true;
        result.new_path  = req.store_path;
        result.new_index = req.artifact_index;
        return result;
    }

    std::vector<core::Artifact> buffer;
    buffer.reserve(pings.size());
    for (auto& ping : pings)
        buffer.emplace_back(std::move(ping));

    core::ArtifactIndex out_index;
    if (!io::writeArtifactBufferToCache(write_path, buffer, meta, out_index)) {
        result.error = "Failed to write corrected data to " + write_path;
        return result;
    }

    out_index.source_id = req.artifact_index.source_id;
    result.ok       = true;
    result.new_path = write_path;
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
    const SidescanCorrectionParams& params,
    std::vector<core::SidescanPing> viewer_pings)
{
    CorrectionRequest req;
    req.layer_id       = layer_id;
    req.store_path     = store_path;
    req.store_format   = store_format;
    req.source_path    = source_path;
    req.artifact_index = artifact_index;
    req.params         = params;
    req.viewer_pings   = std::move(viewer_pings);

    ImportService* svc = m_import_service;
    auto* watcher = new QFutureWatcher<CorrectionResult>(this);
    connect(watcher, &QFutureWatcher<CorrectionResult>::finished, this,
            [this, watcher, layer_id]() {
                watcher->deleteLater();
                CorrectionResult res;
                try { res = watcher->result(); } catch (...) {
                    emit applyFailed(layer_id, "Unexpected exception in correction thread");
                    return;
                }
                if (!res.ok) {
                    emit applyFailed(res.layer_id, res.error);
                } else if (res.skipped) {
                    emit applySkipped(res.layer_id);
                } else {
                    emit correctionsPersisted(res.layer_id, res.new_path, res.new_index);
                }
            });
    watcher->setFuture(QtConcurrent::run([req, svc]() {
        return execute(req, svc);
    }));
}


} // namespace dolphin::app
