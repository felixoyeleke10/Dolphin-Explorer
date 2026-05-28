// ImportService.Load.cpp — read-path methods for ImportService
//
// loadSidescanWindow, loadAllSidescanPings, loadAllSidescanPingsFromStore,
// loadSidescanWindowFromStore, loadAllSubBottomTraces
//
// The write-path (importFile, reindexLayer) lives in ImportService.cpp.

#include "app/services/ImportService.Private.h"
#include "io/jsf/JsfReader.h"
#include "io/cache/ParsedCache.h"
#include "io/segy/SegyReader.h"
#include "io/xtf/XtfReader.h"
#include "core/MagSample.h"
#include "core/SubBottomTrace.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <variant>

namespace dolphin::app {

namespace {

using import_detail::normaliseFormat;
using import_detail::makeReader;

static std::string formatFromPath(const std::string& path, const std::string& fallback = "xtf")
{
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return fallback;
    return normaliseFormat(path.substr(dot + 1));
}

// ── Read-path-only types and helpers ─────────────────────────────────────────

struct ArtifactStoreLocation {
    std::string path;
    std::string format;
};

struct ArtifactStoreSession {
    std::string                     path;
    std::string                     format;
    core::ArtifactIndex             artifact_index;
    std::unique_ptr<io::IFormatReader> reader;
};

static void copySidescanEntries(const core::ArtifactIndex& index,
                                std::vector<core::ArtifactIndexEntry>& out_entries)
{
    out_entries.clear();
    const auto sidescan_entries = index.byType(core::ArtifactType::Sidescan);
    out_entries.reserve(sidescan_entries.size());
    for (const auto* entry : sidescan_entries) {
        if (entry)
            out_entries.push_back(*entry);
    }
}

static void compactSidescanSamples(core::SidescanPing& ping, int max_samples_per_ping)
{
    if (max_samples_per_ping < 2)
        return;

    const size_t max_samples = static_cast<size_t>(max_samples_per_ping);
    const size_t n = ping.samples.size();
    if (n <= max_samples)
        return;

    std::vector<core::SidescanSample> compact;
    compact.reserve(max_samples);

    const size_t last = n - 1;
    const size_t denom = max_samples - 1;
    for (size_t i = 0; i < max_samples; ++i) {
        const size_t idx = (i * last) / denom;
        compact.push_back(ping.samples[idx]);
    }

    ping.samples = std::move(compact);
}

static ArtifactStoreLocation resolveArtifactStore(const DataLayer* layer,
                                                  const std::string& fallback_path)
{
    ArtifactStoreLocation location;
    location.path = fallback_path;
    location.format = formatFromPath(fallback_path);

    if (layer && !layer->artifact_store_path.empty()) {
        location.path = layer->artifact_store_path;
        location.format = !layer->artifact_store_format.empty()
            ? normaliseFormat(layer->artifact_store_format)
            : formatFromPath(location.path);
    }

    return location;
}

static bool openArtifactStoreSession(const ArtifactStoreLocation& location,
                                     const core::ArtifactIndex* preferred_index,
                                     ArtifactStoreSession& out_session)
{
    if (location.path.empty())
        return false;

    out_session = {};
    out_session.path = location.path;
    out_session.format = location.format.empty()
        ? formatFromPath(location.path)
        : normaliseFormat(location.format);
    out_session.reader = makeReader(out_session.format);
    if (!out_session.reader->open(out_session.path)) {
        out_session = {};
        return false;
    }

    if (preferred_index && !preferred_index->empty())
        out_session.artifact_index = *preferred_index;
    else
        out_session.artifact_index = out_session.reader->buildIndex();

    if (out_session.artifact_index.empty()) {
        out_session = {};
        return false;
    }

    return true;
}

static bool openSidescanSession(const DataLayer* layer,
                                const std::string& source_path,
                                ArtifactStoreSession& out_session,
                                std::vector<core::ArtifactIndexEntry>& out_entries)
{
    if (!layer)
        return false;

    const ArtifactStoreLocation primary = resolveArtifactStore(layer, source_path);
    if (openArtifactStoreSession(primary, &layer->artifact_index, out_session)) {
        copySidescanEntries(out_session.artifact_index, out_entries);
        return true;
    }

    if (primary.path == source_path || source_path.empty())
        return false;

    const ArtifactStoreLocation fallback{source_path, formatFromPath(source_path)};
    if (!openArtifactStoreSession(fallback, nullptr, out_session))
        return false;

    copySidescanEntries(out_session.artifact_index, out_entries);
    return true;
}

} // namespace

// ── loadSidescanWindow ────────────────────────────────────────────────────────

std::vector<core::SidescanPing>
ImportService::loadSidescanWindow(const DataLayer* layer,
                                  const std::string& source_path,
                                  int64_t center_ping,
                                  int max_rows) const
{
    std::vector<core::SidescanPing> result;
    if (!layer || !layer->index_built || max_rows <= 0) return result;

    ArtifactStoreSession session;
    std::vector<core::ArtifactIndexEntry> sidescan_entries;
    if (!openSidescanSession(layer, source_path, session, sidescan_entries))
        return result;
    if (sidescan_entries.empty()) return result;

    int64_t total = static_cast<int64_t>(sidescan_entries.size());
    center_ping = std::clamp<int64_t>(center_ping, 0, total - 1);

    int64_t window = std::max<int64_t>(1, max_rows);
    int64_t start  = std::max<int64_t>(0, center_ping - window / 2);
    int64_t end    = std::min<int64_t>(total, start + window);
    start          = std::max<int64_t>(0, end - window);

    result.reserve(static_cast<size_t>(end - start));
    for (int64_t i = start; i < end; ++i) {
        auto artifact = session.reader->readArtifact(sidescan_entries[static_cast<size_t>(i)]);
        if (!artifact || !std::holds_alternative<core::SidescanPing>(*artifact))
            continue;
        result.push_back(std::get<core::SidescanPing>(*artifact));
    }

    return result;
}

// ── loadAllSidescanPings ──────────────────────────────────────────────────────

std::vector<core::SidescanPing>
ImportService::loadAllSidescanPings(const DataLayer* layer,
                                    const std::string& source_path) const
{
    std::vector<core::SidescanPing> result;
    if (!layer || !layer->index_built) return result;

    ArtifactStoreSession session;
    std::vector<core::ArtifactIndexEntry> sidescan_entries;
    if (!openSidescanSession(layer, source_path, session, sidescan_entries))
        return result;

    result.reserve(sidescan_entries.size());
    for (const auto& entry : sidescan_entries) {
        auto artifact = session.reader->readArtifact(entry);
        if (!artifact || !std::holds_alternative<core::SidescanPing>(*artifact))
            continue;
        result.push_back(std::get<core::SidescanPing>(*artifact));
    }
    return result;
}

// ── loadAllSidescanPingsFromStore ─────────────────────────────────────────────

std::vector<core::SidescanPing>
ImportService::loadAllSidescanPingsFromStore(
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path,
    int max_samples_per_ping) const
{
    std::vector<core::SidescanPing> result;
    if (artifact_index.empty()) return result;

    ArtifactStoreLocation location;
    if (!store_path.empty()) {
        location.path   = store_path;
        location.format = store_format.empty()
            ? formatFromPath(store_path)
            : normaliseFormat(store_format);
    } else {
        location.path   = source_path;
        location.format = formatFromPath(source_path);
    }

    ArtifactStoreSession session;
    if (!openArtifactStoreSession(location, &artifact_index, session))
        return result;

    std::vector<core::ArtifactIndexEntry> sidescan_entries;
    copySidescanEntries(session.artifact_index, sidescan_entries);

    result.reserve(sidescan_entries.size());
    for (const auto& entry : sidescan_entries) {
        auto artifact = session.reader->readArtifact(entry);
        if (!artifact || !std::holds_alternative<core::SidescanPing>(*artifact))
            continue;
        auto ping = std::get<core::SidescanPing>(std::move(*artifact));
        compactSidescanSamples(ping, max_samples_per_ping);
        result.push_back(std::move(ping));
    }
    return result;
}

// ── loadAllSidescanNavFromStore ───────────────────────────────────────────────

std::vector<core::SidescanPing>
ImportService::loadAllSidescanNavFromStore(
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path) const
{
    std::vector<core::SidescanPing> result;
    if (artifact_index.empty()) return result;

    ArtifactStoreLocation location;
    if (!store_path.empty()) {
        location.path   = store_path;
        location.format = store_format.empty()
            ? formatFromPath(store_path)
            : normaliseFormat(store_format);
    } else {
        location.path   = source_path;
        location.format = formatFromPath(source_path);
    }

    ArtifactStoreSession session;
    if (!openArtifactStoreSession(location, &artifact_index, session))
        return result;

    std::vector<core::ArtifactIndexEntry> sidescan_entries;
    copySidescanEntries(session.artifact_index, sidescan_entries);

    result.reserve(sidescan_entries.size());
    for (const auto& entry : sidescan_entries) {
        auto artifact = session.reader->readArtifact(entry);
        if (!artifact || !std::holds_alternative<core::SidescanPing>(*artifact))
            continue;
        auto ping = std::get<core::SidescanPing>(std::move(*artifact));
        ping.samples.clear();  // discard amplitude data — nav fields are all we need
        result.push_back(std::move(ping));
    }
    return result;
}

// ── loadSidescanWindowFromStore ───────────────────────────────────────────────

std::vector<core::SidescanPing>
ImportService::loadSidescanWindowFromStore(
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path,
    int64_t center_entry,
    int     max_rows,
    float   frequency_hz) const
{
    std::vector<core::SidescanPing> result;
    if (artifact_index.empty() || max_rows <= 0) return result;

    ArtifactStoreLocation location;
    if (!store_path.empty()) {
        location.path   = store_path;
        location.format = store_format.empty()
            ? formatFromPath(store_path)
            : normaliseFormat(store_format);
    } else {
        location.path   = source_path;
        location.format = formatFromPath(source_path);
    }

    ArtifactStoreSession session;
    if (!openArtifactStoreSession(location, &artifact_index, session))
        return result;

    std::vector<core::ArtifactIndexEntry> sidescan_entries;
    copySidescanEntries(session.artifact_index, sidescan_entries);
    if (sidescan_entries.empty()) return result;

    // Apply frequency band filter before windowing so center_entry indexes
    // into the filtered set and the scrollbar range stays consistent.
    if (frequency_hz > 0.f) {
        std::vector<float> bands;
        for (const auto& e : sidescan_entries) {
            if (e.frequency_hz <= 0.f) continue;
            bool found = false;
            for (float b : bands)
                if (std::fabs(b - e.frequency_hz) < 1.f) { found = true; break; }
            if (!found) bands.push_back(e.frequency_hz);
        }
        if (bands.size() >= 2) {
            float target_band = bands[0];
            for (float b : bands)
                if (std::fabs(b - frequency_hz) < std::fabs(target_band - frequency_hz))
                    target_band = b;
            sidescan_entries.erase(
                std::remove_if(sidescan_entries.begin(), sidescan_entries.end(),
                    [target_band](const core::ArtifactIndexEntry& e) {
                        return e.frequency_hz <= 0.f
                            || std::fabs(e.frequency_hz - target_band) >= 1.f;
                    }),
                sidescan_entries.end());
        }
    }
    if (sidescan_entries.empty()) return result;

    const int64_t total  = static_cast<int64_t>(sidescan_entries.size());
    center_entry         = std::clamp<int64_t>(center_entry, 0, total - 1);
    const int64_t window = static_cast<int64_t>(max_rows);
    int64_t       start  = std::max<int64_t>(0, center_entry - window / 2);
    int64_t       end    = std::min<int64_t>(total, start + window);
    start                = std::max<int64_t>(0, end - window);

    // Port and starboard entries from the same sonar sweep share identical
    // timestamp_us (same packet) and the same ping_number when nonzero.
    // If the window boundary falls between them, expand by one so the assembler
    // always receives complete pairs.
    auto isPaired = [&](int64_t a, int64_t b) -> bool {
        const auto& ea = sidescan_entries[static_cast<size_t>(a)];
        const auto& eb = sidescan_entries[static_cast<size_t>(b)];
        return ea.timestamp_us == eb.timestamp_us
            || (ea.ping_number != 0 && ea.ping_number == eb.ping_number);
    };
    if (start > 0 && isPaired(start - 1, start))
        --start;
    if (end < total && isPaired(end - 1, end))
        ++end;

    result.reserve(static_cast<size_t>(end - start));
    for (int64_t i = start; i < end; ++i) {
        auto artifact = session.reader->readArtifact(
            sidescan_entries[static_cast<size_t>(i)]);
        if (!artifact || !std::holds_alternative<core::SidescanPing>(*artifact))
            continue;
        result.push_back(std::get<core::SidescanPing>(*artifact));
    }
    return result;
}

std::vector<core::SubBottomTrace> ImportService::loadAllSubBottomTraces(
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path) const
{
    std::vector<core::SubBottomTrace> result;

    // Resolve the file to read: prefer artifact store, fall back to raw source
    ArtifactStoreLocation location;
    if (!store_path.empty()) {
        location.path   = store_path;
        location.format = store_format.empty()
            ? formatFromPath(store_path, "segy")
            : normaliseFormat(store_format);
    } else {
        location.path   = source_path;
        location.format = formatFromPath(source_path, "segy");
    }

    if (location.path.empty()) return result;

    auto reader = makeReader(location.format);
    if (!reader->open(location.path)) return result;

    for (const auto& entry : artifact_index.entries) {
        if (entry.type != core::ArtifactType::SubBottom) continue;
        auto art = reader->readArtifact(entry);
        if (!art) continue;
        if (auto* t = std::get_if<core::SubBottomTrace>(&*art))
            result.push_back(std::move(*t));
    }
    return result;
}

// ── loadMagSamples ────────────────────────────────────────────────────────────

std::vector<core::MagSample> ImportService::loadMagSamples(
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path) const
{
    std::vector<core::MagSample> result;

    ArtifactStoreLocation location;
    if (!store_path.empty()) {
        location.path   = store_path;
        location.format = store_format.empty()
            ? formatFromPath(store_path)
            : normaliseFormat(store_format);
    } else {
        location.path   = source_path;
        location.format = formatFromPath(source_path);
    }
    if (location.path.empty()) return result;

    auto reader = makeReader(location.format);
    if (!reader->open(location.path)) return result;

    for (const auto& entry : artifact_index.entries) {
        if (entry.type != core::ArtifactType::Magnetometer) continue;
        auto art = reader->readArtifact(entry);
        if (!art) continue;
        if (auto* m = std::get_if<core::MagSample>(&*art))
            result.push_back(std::move(*m));
    }
    return result;
}

// ── loadArtifacts (generic) ───────────────────────────────────────────────────

std::vector<core::Artifact> ImportService::loadArtifacts(
    core::ArtifactType type,
    const std::string& store_path,
    const std::string& store_format,
    const core::ArtifactIndex& artifact_index,
    const std::string& source_path) const
{
    std::vector<core::Artifact> result;

    ArtifactStoreLocation location;
    if (!store_path.empty()) {
        location.path   = store_path;
        location.format = store_format.empty()
            ? formatFromPath(store_path)
            : normaliseFormat(store_format);
    } else {
        location.path   = source_path;
        location.format = formatFromPath(source_path);
    }
    if (location.path.empty()) return result;

    auto reader = makeReader(location.format);
    if (!reader->open(location.path)) return result;

    for (const auto& entry : artifact_index.entries) {
        if (entry.type != type) continue;
        auto art = reader->readArtifact(entry);
        if (art) result.push_back(std::move(*art));
    }
    return result;
}

} // namespace dolphin::app
