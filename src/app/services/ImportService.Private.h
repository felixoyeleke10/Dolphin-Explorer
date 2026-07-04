// ImportService.Private.h — types and helpers shared between
// ImportService.cpp and ImportService.Tasks.cpp.
#pragma once
#include "app/services/ImportService.h"
#include "app/layers/DataLayer.h"
#include "core/Artifact.h"
#include "io/IFormatReader.h"
#include <memory>
#include <string>
#include <vector>

namespace dolphin::app::import_detail {

struct ImportTaskResult {
    core::ArtifactIndex artifact_index;
    std::string         artifact_store_path;
    std::string         artifact_store_format;
    core::SpatialRef    source_spatial_ref;
    uint64_t            source_size_bytes = 0;
    int64_t             source_modified_utc_ms = 0;
    std::string         error;
    std::string         sonar_name;
    std::string         survey_name;
    std::string         vessel_name;
    double              start_time_utc = 0.0;
    double              end_time_utc = 0.0;
    float               frequency_hz     = 0.0f;
    float               low_frequency_hz = 0.0f;
    BottomTrackKind     bottom_track_kind = BottomTrackKind::Unknown;
    // True when buildArtifactStore re-decoded the source and rewrote the cache (vs.
    // reusing a fingerprint-matched cache). Lets completeImport refresh sibling
    // layers' indices when a modality is added to an existing source whose cache
    // had to be rebuilt — otherwise their offsets would point into the old cache.
    bool                cache_rebuilt = false;
};

struct SourceFingerprint {
    uint64_t size_bytes = 0;
    int64_t  modified_utc_ms = 0;
    bool     exists = false;
};

// Helpers defined in ImportService.cpp.
void copyImportMetadata(const io::FormatMeta& meta, ImportTaskResult& result);
void applyImportResultToLayer(DataLayer& layer, const ImportTaskResult& result);
void applyImportResultToSource(ProjectSource& source, const ImportTaskResult& result);
std::string normaliseFormat(std::string format);
std::unique_ptr<io::IFormatReader> makeReader(const std::string& format);
std::string uniqueLayerLabel(const Project& project, const std::string& base_label);
SourceFingerprint inspectSourceFile(const std::string& path);
bool sourceFingerprintMatches(const ProjectSource& source, const SourceFingerprint& fingerprint);
void releaseSourceJob(const std::string& source_id);
void removeArtifactStoreFileIfUnused(const Project& project,
                                     const std::string& source_id,
                                     const ImportTaskResult& result);

// Task functions defined in ImportService.Tasks.cpp.
ImportTaskResult buildArtifactStore(const ProjectSource& source,
                                    const std::string& raw_path,
                                    const std::string& raw_format,
                                    const std::string& cache_path,
                                    io::ProgressFn progress);
void completeImport(ImportService* svc,
                    ImportTaskResult result,
                    std::shared_ptr<Project> project,
                    const std::string& layer_id,
                    const std::string& source_id,
                    const core::SpatialRef& user_crs,
                    bool import_hf,
                    bool import_lf,
                    std::vector<core::ArtifactType> wanted_modules);
void completeReindex(ImportService* svc,
                     ImportTaskResult result,
                     std::shared_ptr<Project> project,
                     const std::string& layer_id,
                     const std::string& source_id,
                     const core::SpatialRef& user_crs = {});

} // namespace dolphin::app::import_detail
