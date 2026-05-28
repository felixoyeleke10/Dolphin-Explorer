#include "pipeline/nodes/merge/MergeNode.h"
#include <algorithm>

namespace dolphin::pipeline {

// ── MergeNode ─────────────────────────────────────────────────────────────────

NodeSchema MergeNode::schema() const
{
    return NodeSchema{ "merge", "Merge", "Merge", {} };
}

ArtifactBuffer MergeNode::process(const ArtifactBuffer& input, const NodeParams&) const
{
    // The executor collects all upstream artifacts into `input` via appendUnique.
    // Merge just passes the combined buffer through.
    return input;
}

// ── BlendNode ─────────────────────────────────────────────────────────────────

NodeSchema BlendNode::schema() const
{
    return NodeSchema{
        "blend", "Blend", "Merge",
        {
            { "mix", { "Mix (A/B)", 0.5f, 0.0f, 1.0f, {} } },
        }
    };
}

ArtifactBuffer BlendNode::process(const ArtifactBuffer& input, const NodeParams&) const
{
    // Pass both inputs through — actual blend weight applied by downstream renderer.
    return input;
}

// ── MultiMergeNode ────────────────────────────────────────────────────────────

int MultiMergeNode::inputCount() const
{
    auto it = params.find("inputs");
    if (it != params.end()) {
        if (auto* v = std::get_if<int>(&it->second))
            return std::clamp(*v, 2, 8);
        if (auto* v = std::get_if<float>(&it->second))
            return std::clamp((int)*v, 2, 8);
    }
    return 4;
}

NodeSchema MultiMergeNode::schema() const
{
    return NodeSchema{
        "multi_merge", "MultiMerge", "Merge",
        {
            { "inputs", { "Input Count", 4, 2, 8, {} } },
        }
    };
}

ArtifactBuffer MultiMergeNode::process(const ArtifactBuffer& input, const NodeParams&) const
{
    return input;
}

} // namespace dolphin::pipeline
