#pragma once

#include <string>
#include <variant>
#include <vector>

#include "core/ArtifactIndex.h"
#include "app/layers/LayerUtils.h"   // Modality

namespace dolphin::app::contracts {

enum class ContractType {
    ProcessedLayer,
    CorrectionApplied,
    BottomTrackResult,
    QCFlags,
    SurveyStatistics,
    ReportPackage,
};

std::string contractTypeName(ContractType type);

// Output of a processing run on a layer of any modality. (Renamed from
// ProcessedSidescanLayer + given a `modality` so it is honest for SBP/MAG layers
// too, not just sidescan.)
struct ProcessedLayer {
    std::string         layer_id;
    std::string         source_id;
    std::string         artifact_store_path;
    std::string         artifact_store_format;
    core::ArtifactIndex artifact_index;
    std::string         graph_hash;
    Modality            modality = Modality::Sidescan;
};

struct CorrectionApplied {
    std::string         layer_id;
    std::string         artifact_store_path;
    core::ArtifactIndex artifact_index;
};

struct BottomTrackResult {
    std::string        layer_id;
    std::vector<float> bottom_samples;
    float              confidence_mean = 0.0f;
};

struct QCFlags {
    std::string              layer_id;
    std::vector<std::string> warnings;
    std::vector<std::string> failures;
    bool                     pass = true;
};

struct SurveyStatistics {
    std::string layer_id;
    size_t      sidescan_ping_count = 0;
    double      track_length_m = 0.0;
    double      coverage_m2 = 0.0;
};

struct ReportPackage {
    std::string              report_id;
    std::string              output_dir;
    std::vector<std::string> files;
};

using ContractPayload = std::variant<
    ProcessedLayer,
    CorrectionApplied,
    BottomTrackResult,
    QCFlags,
    SurveyStatistics,
    ReportPackage>;

} // namespace dolphin::app::contracts
