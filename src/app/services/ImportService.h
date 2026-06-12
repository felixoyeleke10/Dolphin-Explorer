#pragma once
#include <QObject>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "app/project/Project.h"
#include "app/tasks/CancellationToken.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/MagSample.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include "core/SpatialRef.h"

namespace dolphin::app {

// Owns the file import workflow.
//
// Callers hand it a path + project and connect to its signals.
// They do not drive the indexing loop or create DataLayers themselves.
// This is the only place in the codebase that should #include XtfReader.h.
class ImportService : public QObject {
    Q_OBJECT
public:
    explicit ImportService(QObject* parent = nullptr);
    ~ImportService() override;

    // Import a file into the project asynchronously.
    // Creates a ProjectSource + DataLayer, begins background indexing.
    // Returns the new DataLayer id, or empty string on immediate failure.
    // user_crs: if non-empty, overrides any CRS auto-detected from the file
    // header and is stamped onto the layer/source with exact=true.
    // import_hf / import_lf: for dual-frequency SSS files, controls which pinned
    //   band layers to create.  Both default to true (create _HF and _LF).
    //   Single-frequency files are unaffected — one normal layer is created.
    //   At least one of the two should be true; if both are false the call is
    //   treated as import_hf=true, import_lf=true.
    // wanted_modules: if non-empty, only artifact families in this list are
    //   routed to DataLayers; others are discarded even if present in the file.
    //   Empty (default) = import every family present in the source.
    std::string importFile(const std::string& path,
                           const std::string& format,
                           std::shared_ptr<Project> project,
                           const core::SpatialRef& user_crs = {},
                           bool import_hf = true,
                           bool import_lf = true,
                           std::vector<core::ArtifactType> wanted_modules = {});

    // Rebuild the artifact index for an existing layer asynchronously.
    std::string reindexLayer(const std::string& path,
                             std::shared_ptr<Project> project,
                             const std::string& layer_id);

    // Rebuild the artifact index directly from an existing .dlpd cache without
    // re-parsing the raw source.  Used by loadProject to defer the per-layer
    // buildIndex scan out of fromJson and onto a background thread.
    // Emits cacheIndexRebuilt(layer_id) on success, indexingFailed on error.
    void rebuildCacheIndex(const std::string& layer_id,
                           std::shared_ptr<Project> project);

    // Cancel all in-flight rebuildCacheIndex operations.  The background scans
    // will stop at the next cancellation checkpoint; completion callbacks discard
    // their results.  Called by loadProject when opening a different project.
    void cancelPendingRebuild();

    // True while a rebuildCacheIndex background scan is running for layer_id.
    // Use this to distinguish cache rebuilds from fresh imports in signal handlers.
    bool isRebuildingLayer(const std::string& layer_id) const
        { return m_rebuild_tokens.count(layer_id) > 0; }

    // Load a visible sidescan window around the requested ping index.
    // Returns only decoded sidescan pings in scrubber order.
    std::vector<core::SidescanPing> loadSidescanWindow(const DataLayer* layer,
                                                       const std::string& source_path,
                                                       int64_t center_ping,
                                                       int max_rows) const;

    // Load every sidescan ping in the layer (no windowing).
    std::vector<core::SidescanPing> loadAllSidescanPings(const DataLayer* layer,
                                                         const std::string& source_path) const;

    // Thread-safe overload — takes pre-copied field values instead of a DataLayer*.
    // Safe to call from QtConcurrent::run() without holding the layer pointer.
    std::vector<core::SidescanPing> loadAllSidescanPingsFromStore(
        const std::string& store_path,
        const std::string& store_format,
        const core::ArtifactIndex& artifact_index,
        const std::string& source_path,
        int max_samples_per_ping = 0) const;

    // Load all sub-bottom traces from an artifact store.
    // Thread-safe: takes pre-copied store/index values — do not pass DataLayer* directly.
    // Returns an empty vector on failure (missing store, empty index, read error).
    std::vector<core::SubBottomTrace> loadAllSubBottomTraces(
        const std::string& store_path,
        const std::string& store_format,
        const core::ArtifactIndex& artifact_index,
        const std::string& source_path) const;

    // Load all magnetometer samples from an artifact store.
    // Thread-safe: takes pre-copied store/index values.
    std::vector<core::MagSample> loadMagSamples(
        const std::string& store_path,
        const std::string& store_format,
        const core::ArtifactIndex& artifact_index,
        const std::string& source_path) const;

    // Generic load — returns every artifact of `type` as a variant vector.
    // Use the typed overloads above when you know the concrete type; this
    // overload is for operations that process artifacts uniformly (stats,
    // nav extraction, export) regardless of modality.
    std::vector<core::Artifact> loadArtifacts(
        core::ArtifactType type,
        const std::string& store_path,
        const std::string& store_format,
        const core::ArtifactIndex& artifact_index,
        const std::string& source_path) const;

    // Nav-only variant — loads all pings but discards sample buffers immediately.
    // Use this for metadata/spreadsheet display; avoids storing hundreds of MB
    // of amplitude data that the caller doesn't need.
    std::vector<core::SidescanPing> loadAllSidescanNavFromStore(
        const std::string& store_path,
        const std::string& store_format,
        const core::ArtifactIndex& artifact_index,
        const std::string& source_path) const;

    // Thread-safe windowed load — same as loadAllSidescanPingsFromStore but
    // limits the result to max_rows artifact entries centred on center_entry.
    // center_entry is a 0-based index into the (optionally filtered) sidescan entries.
    // frequency_hz: when > 0, only pings whose frequency is in the nearest band
    //   to this value are returned.  Pass 0 to return all frequencies.
    std::vector<core::SidescanPing> loadSidescanWindowFromStore(
        const std::string& store_path,
        const std::string& store_format,
        const core::ArtifactIndex& artifact_index,
        const std::string& source_path,
        int64_t center_entry,
        int     max_rows,
        float   frequency_hz = 0.f) const;

signals:
    void indexingStarted(const std::string& layer_id);
    void indexingProgress(const std::string& layer_id, int percent);  // 0-100
    void indexingComplete(const std::string& layer_id);
    void indexingFailed(const std::string& layer_id, const std::string& error);
    // Emitted when rebuildCacheIndex completes successfully — distinct from
    // indexingComplete so callers can handle open-time reindex differently from
    // fresh imports (e.g. auto-activate the layer in the map and waterfall).
    void cacheIndexRebuilt(const std::string& layer_id);

private:
    // Cancellation tokens for in-flight rebuildCacheIndex calls, keyed by layer_id.
    std::unordered_map<std::string, CancellationToken> m_rebuild_tokens;
};

} // namespace dolphin::app
