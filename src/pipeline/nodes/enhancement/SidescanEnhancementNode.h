#pragma once

#include "pipeline/IProcessingNode.h"

namespace dolphin::pipeline {

enum class SidescanEnhancementKind { Arn, Destripe, BeamPattern, AdaptiveContrast };

class SidescanEnhancementNode final : public IProcessingNode {
public:
    explicit SidescanEnhancementNode(SidescanEnhancementKind kind) : m_kind(kind) {}

    std::string typeId() const override;
    std::string label() const override;
    NodeSchema schema() const override;
    ArtifactBuffer process(const ArtifactBuffer& input,
                           const NodeParams& params) const override;

private:
    SidescanEnhancementKind m_kind;
};

} // namespace dolphin::pipeline
