#include "app/corrections/SubBottomCorrectionService.h"
#include "app/services/ImportService.h"
#include "io/cache/ParsedCache.h"
#include "core/Artifact.h"
#include "core/SubBottomTrace.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
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
};

SbpCorrectionResult execute(const SbpCorrectionRequest& req)
{
    SbpCorrectionResult result;
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

    auto traces = ImportService::loadAllSubBottomTraces(
        req.store_path, req.store_format, req.artifact_index, req.source_path);

    if (traces.empty()) {
        result.error = "No sub-bottom traces loaded from " + req.store_path;
        return result;
    }

    // AND of all trace flags: only skip a correction if every trace already has it.
    uint32_t baked = ~0u;
    for (const auto& t : traces) baked &= t.correction_flags;
    bool modified = false;

    for (auto& t : traces) {
        auto& s = t.samples;
        if (s.empty()) continue;
        const int n = static_cast<int>(s.size());

        if (req.signal.dc_removal_en &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::DcRemoval)) {
            float sum = 0.f;
            for (float v : s) sum += v;
            const float mean = sum / static_cast<float>(n);
            for (float& v : s) v -= mean;
            t.correction_flags |= core::SbpCorrectionFlag::DcRemoval;
            modified = true;
        }

        if (req.signal.envelope_en &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::Envelope)) {
            for (float& v : s) v = std::abs(v);
            t.correction_flags |= core::SbpCorrectionFlag::Envelope;
            modified = true;
        }

        if (req.gain.normalize_en &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::Normalize)) {
            float mx = 0.f;
            for (float v : s) mx = std::max(mx, std::abs(v));
            if (mx > 0.f)
                for (float& v : s) v /= mx;
            t.correction_flags |= core::SbpCorrectionFlag::Normalize;
            modified = true;
        }

        if (req.gain.static_gain_en && req.gain.static_gain_db != 0.f &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::StaticGain)) {
            const float factor = std::pow(10.f, req.gain.static_gain_db / 20.f);
            for (float& v : s) v *= factor;
            t.correction_flags |= core::SbpCorrectionFlag::StaticGain;
            modified = true;
        }
    }

    if (req.gain.agc_en &&
        !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::Agc)) {
        const int n_t = static_cast<int>(traces.size());
        const int hw  = std::max(1, req.gain.agc_window);

        std::vector<float> trace_energy(n_t, 0.f);
        std::vector<int>   trace_count (n_t, 0);
        for (int i = 0; i < n_t; ++i) {
            for (float v : traces[i].samples) trace_energy[i] += v * v;
            trace_count[i] = static_cast<int>(traces[i].samples.size());
        }

        float window_energy = 0.f;
        int   window_count  = 0;
        int   left = 0, right = -1;

        for (int ti = 0; ti < n_t; ++ti) {
            const int new_right = std::min(n_t - 1, ti + hw);
            while (right < new_right) {
                ++right;
                window_energy += trace_energy[right];
                window_count  += trace_count[right];
            }
            const int new_left = std::max(0, ti - hw);
            while (left < new_left) {
                window_energy -= trace_energy[left];
                window_count  -= trace_count[left];
                ++left;
            }
            if (window_count > 0) {
                const float rms = std::sqrt(window_energy / static_cast<float>(window_count));
                if (rms > 0.f)
                    for (float& v : traces[ti].samples) v /= rms;
            }
        }
        for (auto& t : traces) t.correction_flags |= core::SbpCorrectionFlag::Agc;
        modified = true;
    }

    if (!modified) {
        result.ok        = true;
        result.skipped   = true;
        result.new_path  = req.store_path;
        result.new_index = req.artifact_index;
        return result;
    }

    std::vector<core::Artifact> buffer;
    buffer.reserve(traces.size());
    for (auto& t : traces)
        buffer.emplace_back(std::move(t));

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
                    emit correctionsPersisted(res.layer_id, res.new_path, res.new_index);
                }
            });
    watcher->setFuture(QtConcurrent::run([req]() {
        return execute(req);
    }));
}


} // namespace dolphin::app
