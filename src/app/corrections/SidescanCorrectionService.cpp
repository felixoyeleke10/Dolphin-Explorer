#include "app/corrections/SidescanCorrectionService.h"
#include "app/corrections/CorrectionAlgorithms.h"
#include "app/artifacts/ArtifactSidecar.h"
#include "app/services/ImportService.h"
#include "io/cache/ParsedCache.h"
#include "io/xtf/XtfReader.h"
#include "io/jsf/JsfReader.h"
#include "core/Artifact.h"
#include "core/SidescanGeometry.h"
#include "core/SidescanPing.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <cctype>
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

template<typename Eligible, typename Apply>
bool applyToUnbaked(std::vector<core::SidescanPing>& pings,
                    core::CorrectionFlag flag,
                    Eligible&& eligible,
                    Apply&& apply)
{
    std::vector<size_t> indices;
    std::vector<core::SidescanPing> work;
    for (size_t i = 0; i < pings.size(); ++i) {
        if (core::hasCorrectionFlag(pings[i].correction_flags, flag)
            || !eligible(pings[i]))
            continue;
        indices.push_back(i);
        work.push_back(pings[i]);
    }
    if (work.empty()) return false;
    apply(work);
    for (size_t i = 0; i < work.size(); ++i) {
        pings[indices[i]].samples = std::move(work[i].samples);
        pings[indices[i]].correction_flags |= flag;
    }
    return true;
}

CorrectionResult execute(const CorrectionRequest& req)
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
        write_path = dolphin::app::sidecarArtifactPath(
            req.store_path, req.layer_id, meta.artifact_role);
        meta.artifact_role = io::kArtifactRoleSidecar;  // formal marker on the output
    }

    auto pings = ImportService::loadAllSidescanPingsFromStore(
        req.store_path, req.store_format, req.artifact_index, req.source_path);

    if (pings.empty()) {
        result.error = "No sidescan pings loaded from " + req.store_path;
        return result;
    }

    bool modified = false;

    if (req.params.tvg.enabled)
        modified |= applyToUnbaked(pings, core::CorrectionFlag::Tvg,
            [](const auto& ping) {
                return ping.samples.size() >= 2 && ping.slant_range_m > ping.blanking_m;
            },
            [&](auto& work) { corrections::applyTvg(work, req.params.tvg); });

    if (req.params.arc.enabled)
        modified |= applyToUnbaked(pings, core::CorrectionFlag::Arc,
            [](const auto& ping) {
                return ping.samples.size() >= 2 && ping.slant_range_m > 0.0f
                    && core::sidescanAltitudeMetres(ping).has_value();
            },
            [&](auto& work) { corrections::applyArc(work, req.params.arc); });

    if (req.params.agc.enabled)
        modified |= applyToUnbaked(pings, core::CorrectionFlag::GainNormalized,
            [](const auto& ping) { return !ping.samples.empty(); },
            [&](auto& work) { corrections::normalizeAmplitudes(work, req.params.agc); });

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

SidescanCorrectionService::SidescanCorrectionService(QObject* parent)
    : QObject(parent)
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
    watcher->setFuture(QtConcurrent::run([req]() {
        return execute(req);
    }));
}


} // namespace dolphin::app
