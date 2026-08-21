#include "pipeline/nodes/enhancement/SidescanEnhancementNode.h"

#include "core/SidescanPing.h"
#include "pipeline/SidescanEnhancementAlgorithms.h"

#include <algorithm>

namespace dolphin::pipeline {
namespace {

float floatParam(const NodeParams& params, const char* key, float fallback)
{
    const auto found = params.find(key);
    return found == params.end() ? fallback : std::get<float>(found->second);
}

int intParam(const NodeParams& params, const char* key, int fallback)
{
    const auto found = params.find(key);
    return found == params.end() ? fallback : std::get<int>(found->second);
}

core::CorrectionFlag provenance(SidescanEnhancementKind kind)
{
    switch (kind) {
    case SidescanEnhancementKind::Arn: return core::CorrectionFlag::Arn;
    case SidescanEnhancementKind::Destripe: return core::CorrectionFlag::Destriping;
    case SidescanEnhancementKind::BeamPattern: return core::CorrectionFlag::BeamPattern;
    case SidescanEnhancementKind::AdaptiveContrast: return core::CorrectionFlag::AdaptiveContrast;
    }
    return core::CorrectionFlag::None;
}

} // namespace

std::string SidescanEnhancementNode::typeId() const
{
    switch (m_kind) {
    case SidescanEnhancementKind::Arn: return "arn";
    case SidescanEnhancementKind::Destripe: return "destripe";
    case SidescanEnhancementKind::BeamPattern: return "beam_pattern";
    case SidescanEnhancementKind::AdaptiveContrast: return "adaptive_contrast";
    }
    return {};
}

std::string SidescanEnhancementNode::label() const
{
    switch (m_kind) {
    case SidescanEnhancementKind::Arn: return "Adaptive Range Normalisation";
    case SidescanEnhancementKind::Destripe: return "Destripe";
    case SidescanEnhancementKind::BeamPattern: return "Beam Pattern Normalisation";
    case SidescanEnhancementKind::AdaptiveContrast: return "Adaptive Contrast (CLAHE)";
    }
    return {};
}

NodeSchema SidescanEnhancementNode::schema() const
{
    NodeSchema result{typeId(), label(), "Enhancement", {}};
    switch (m_kind) {
    case SidescanEnhancementKind::Arn:
        result.params = {
            {"strength", {"Strength", 0.8f, 0.f, 1.f}},
            {"gain_cap_db", {"Gain cap (dB)", 12.f, 0.f, 40.f}},
            {"column_smooth", {"Range smoothing radius", 5, 0, 1000}},
        };
        break;
    case SidescanEnhancementKind::Destripe:
        result.params = {
            {"window", {"Along-track window (pings)", 50, 1, 5000}},
            {"subdivision", {"Range zones", 4, 1, 64}},
            {"capping", {"Correction cap", 2.f, 1.f, 10.f}},
            {"threshold_db", {"Stripe threshold (dB)", 1.f, 0.f, 12.f}},
        };
        break;
    case SidescanEnhancementKind::BeamPattern:
        result.params = {
            {"strength", {"Strength", 1.f, 0.f, 1.f}},
            {"smooth_radius", {"Profile smoothing radius", 10, 0, 1000}},
            {"gain_cap_db", {"Gain cap (dB)", 12.f, 0.f, 40.f}},
        };
        break;
    case SidescanEnhancementKind::AdaptiveContrast:
        result.params = {
            {"tile_pings", {"Tile height (pings)", 64, 16, 4096}},
            {"tile_samps", {"Tile width (samples)", 128, 16, 8192}},
            {"clip_limit", {"Contrast clip limit", 2.f, 1.f, 16.f}},
        };
        break;
    }
    return result;
}

ArtifactBuffer SidescanEnhancementNode::process(const ArtifactBuffer& input,
                                                 const NodeParams& params) const
{
    ArtifactBuffer output = input;
    const auto flag = provenance(m_kind);
    for (const auto channel : {core::SidescanChannel::Port,
                               core::SidescanChannel::Starboard}) {
        enhancement::AmplitudeRows rows;
        std::vector<core::SidescanPing*> pings;
        for (auto& artifact : output) {
            auto* ping = std::get_if<core::SidescanPing>(&artifact);
            if (!ping || ping->channel != channel || core::hasCorrectionFlag(
                    ping->correction_flags, flag)) continue;
            pings.push_back(ping);
        }
        std::sort(pings.begin(), pings.end(), [](const auto* left, const auto* right) {
            if (left->timestamp_us != right->timestamp_us)
                return left->timestamp_us < right->timestamp_us;
            if (left->ping_number != right->ping_number)
                return left->ping_number < right->ping_number;
            return left->id < right->id;
        });
        rows.reserve(pings.size());
        std::vector<std::vector<uint16_t>> amplitudes(pings.size());
        for (size_t row = 0; row < pings.size(); ++row) {
            amplitudes[row].reserve(pings[row]->samples.size());
            for (const auto& sample : pings[row]->samples)
                amplitudes[row].push_back(sample.amplitude);
            rows.push_back(&amplitudes[row]);
        }
        if (rows.empty()) continue;
        bool modified = false;
        switch (m_kind) {
        case SidescanEnhancementKind::Arn:
            modified = enhancement::applyArn(rows, {true, floatParam(params, "strength", 0.8f),
                floatParam(params, "gain_cap_db", 12.f), intParam(params, "column_smooth", 5)});
            break;
        case SidescanEnhancementKind::Destripe:
            modified = enhancement::applyDestripe(rows, {true, intParam(params, "window", 50),
                intParam(params, "subdivision", 4), floatParam(params, "capping", 2.f),
                floatParam(params, "threshold_db", 1.f)});
            break;
        case SidescanEnhancementKind::BeamPattern:
            modified = enhancement::applyBeamPattern(rows, {true, floatParam(params, "strength", 1.f),
                intParam(params, "smooth_radius", 10), floatParam(params, "gain_cap_db", 12.f)});
            break;
        case SidescanEnhancementKind::AdaptiveContrast:
            modified = enhancement::applyAdaptiveContrast(rows, {true,
                intParam(params, "tile_pings", 64), intParam(params, "tile_samps", 128),
                floatParam(params, "clip_limit", 2.f)});
            break;
        }
        for (size_t row = 0; row < pings.size(); ++row) {
            for (size_t sample = 0; sample < amplitudes[row].size(); ++sample)
                pings[row]->samples[sample].amplitude = amplitudes[row][sample];
            if (modified) pings[row]->correction_flags |= flag;
        }
    }
    return output;
}

} // namespace dolphin::pipeline
