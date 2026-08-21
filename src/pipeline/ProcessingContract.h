#pragma once

#include "pipeline/IProcessingNode.h"

#include <cstdint>
#include <string>

namespace dolphin::pipeline {

enum class ProcessingStage : uint8_t {
    Input, Navigation, Detection, Radiometry, Geometry, Filter,
    Normalization, Enhancement, Analysis, Merge, Output
};

struct ProcessingContract {
    uint32_t supported_artifacts = 0;
    ProcessingStage stage = ProcessingStage::Enhancement;
    bool mutates_samples = false;
    bool idempotent = false;
};

// Central policy for every built-in node. Unknown/plugin nodes fail closed until
// they explicitly register a modality/stage contract here.
ProcessingContract processingContractFor(const std::string& type_id);

// Validates schema values, modality compatibility and scientifically invalid
// upstream state. Empty means the invocation is valid.
std::string validateProcessingInvocation(const IProcessingNode& node,
                                         const ArtifactBuffer& input);

// Validates the scientific stage relationship for a connected graph edge.
std::string validateProcessingOrder(const IProcessingNode& upstream,
                                    const IProcessingNode& downstream);

} // namespace dolphin::pipeline
