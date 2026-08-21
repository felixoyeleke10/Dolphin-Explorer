#include "pipeline/ProcessingContract.h"

#include "core/Artifact.h"
#include "core/SidescanGeometry.h"
#include "core/SidescanPing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace dolphin::pipeline {
namespace {

constexpr uint32_t bit(core::ArtifactType type)
{
    return 1u << static_cast<unsigned>(type);
}
constexpr uint32_t kSss = bit(core::ArtifactType::Sidescan);
constexpr uint32_t kSbp = bit(core::ArtifactType::SubBottom);
constexpr uint32_t kMbes = bit(core::ArtifactType::Multibeam);
constexpr uint32_t kAll = (1u << 5u) - 1u;

bool sameValueKind(const Value& a, const Value& b)
{
    return a.index() == b.index();
}

double numericValue(const Value& value)
{
    return std::visit([](const auto& v) -> double {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_arithmetic_v<T>) return static_cast<double>(v);
        return 0.0;
    }, value);
}

bool isNumeric(const Value& value)
{
    return !std::holds_alternative<std::string>(value)
        && !std::holds_alternative<bool>(value);
}

} // namespace

ProcessingContract processingContractFor(const std::string& id)
{
    using S = ProcessingStage;
    static const std::map<std::string, ProcessingContract> contracts = {
        {"sss_input",       {kSss,  S::Input,         false, true}},
        {"sbp_input",       {kSbp,  S::Input,         false, true}},
        {"mbes_input",      {kMbes, S::Input,         false, true}},
        {"nav_smooth",      {kSss,  S::Navigation,    false, true}},
        {"geocorrect",      {kSss,  S::Navigation,    false, true}},
        {"bottom_detect",   {kSss,  S::Detection,     false, true}},
        {"tvg",             {kSss,  S::Radiometry,    true,  true}},
        {"arc",             {kSss,  S::Radiometry,    true,  true}},
        {"slant_range",     {kSss,  S::Geometry,      false, true}},
        {"clip_range",      {kSss,  S::Geometry,      true,  false}},
        {"bpf",             {kSss,  S::Filter,        true,  true}},
        {"speckle",         {kSss,  S::Filter,        true,  false}},
        {"decimate",        {kAll,  S::Filter,        false, false}},
        {"gain_normalize",  {kSss,  S::Normalization, true,  true}},
        {"arn",             {kSss,  S::Normalization, true,  true}},
        {"destripe",        {kSss,  S::Normalization, true,  true}},
        {"beam_pattern",    {kSss,  S::Normalization, true,  true}},
        {"adaptive_contrast",{kSss, S::Enhancement,   true,  true}},
        {"contrast_enhance",{kSss,  S::Enhancement,   true,  true}},
        {"histogram_eq",    {kSss,  S::Enhancement,   true,  true}},
        {"shadow_detect",   {kSss,  S::Analysis,      true,  false}},
        {"merge",           {kAll,  S::Merge,         false, true}},
        {"blend",           {kAll,  S::Merge,         false, true}},
        {"multi_merge",     {kAll,  S::Merge,         false, true}},
        {"export_geotiff",  {kAll,  S::Output,        false, true}},
        {"export_csv",      {kAll,  S::Output,        false, true}},
    };
    const auto it = contracts.find(id);
    // Fail closed: a newly registered processing node must declare its
    // modality/stage contract here before it is allowed to execute.
    return it != contracts.end() ? it->second
                                 : ProcessingContract{0u, S::Enhancement, false, false};
}

std::string validateProcessingInvocation(const IProcessingNode& node,
                                         const ArtifactBuffer& input)
{
    const NodeSchema schema = node.schema();
    for (const auto& [key, value] : node.params) {
        const auto spec_it = schema.params.find(key);
        if (spec_it == schema.params.end())
            return "unsupported parameter '" + key + "'";
        const auto& spec = spec_it->second;
        if (!sameValueKind(value, spec.default_value))
            return "parameter '" + key + "' has the wrong value type";
        if (isNumeric(value)) {
            const double number = numericValue(value);
            const double low = numericValue(spec.min_value);
            const double high = numericValue(spec.max_value);
            if (!std::isfinite(number) || number < low || number > high) {
                std::ostringstream message;
                message << "parameter '" << key << "' is outside ["
                        << low << ", " << high << "]";
                return message.str();
            }
        } else if (const auto* text = std::get_if<std::string>(&value);
                   text && !spec.options.empty()
                   && std::find(spec.options.begin(), spec.options.end(), *text)
                        == spec.options.end()) {
            return "parameter '" + key + "' is not an allowed option";
        }
    }

    const auto contract = processingContractFor(node.typeId());
    if (contract.supported_artifacts == 0u)
        return "node type has no registered processing contract";
    bool has_supported = input.empty();
    for (const auto& artifact : input) {
        if ((contract.supported_artifacts & bit(core::artifactType(artifact))) != 0)
            has_supported = true;
    }
    if (!has_supported)
        return "node is incompatible with the incoming artifact modality";

    // TVG and ARC require slant ranges. Running either after SRC would silently
    // feed ground range into slant-range physics, producing plausible but wrong
    // amplitudes. Block that entire class of graph-ordering error centrally.
    if (node.typeId() == "tvg" || node.typeId() == "arc") {
        for (const auto& artifact : input) {
            const auto* ping = std::get_if<core::SidescanPing>(&artifact);
            if (ping && core::hasCorrectionFlag(
                    ping->correction_flags, core::CorrectionFlag::SlantRange))
                return node.label() + " must run before Slant-Range Correction";
        }
    }
    if (node.typeId() == "arc") {
        bool all_already_corrected = true;
        bool has_geometry = false;
        for (const auto& artifact : input) {
            const auto* ping = std::get_if<core::SidescanPing>(&artifact);
            if (!ping) continue;
            if (core::hasCorrectionFlag(
                    ping->correction_flags, core::CorrectionFlag::Arc))
                continue;
            all_already_corrected = false;
            const auto altitude = core::sidescanAltitudeMetres(*ping);
            if (!altitude) continue;
            for (size_t i = 0; i < ping->samples.size(); ++i) {
                const auto range = core::sidescanSampleRangeMetres(*ping, i);
                if (range && *range > *altitude) {
                    has_geometry = true;
                    break;
                }
            }
            if (has_geometry) break;
        }
        if (!all_already_corrected && !has_geometry)
            return "ARC requires a seabed bottom pick or navigation altitude "
                   "and samples beyond the seabed";
    }
    if (node.typeId() == "beam_pattern") {
        for (const auto& artifact : input) {
            const auto* ping = std::get_if<core::SidescanPing>(&artifact);
            if (!ping || core::hasCorrectionFlag(
                    ping->correction_flags, core::CorrectionFlag::BeamPattern))
                continue;
            if (!core::hasCorrectionFlag(
                    ping->correction_flags, core::CorrectionFlag::SlantRange))
                return "Beam Pattern Normalisation requires Slant-Range Correction first";
        }
    }
    return {};
}

std::string validateProcessingOrder(const IProcessingNode& upstream,
                                    const IProcessingNode& downstream)
{
    if (upstream.typeId() == "arc" && downstream.typeId() == "tvg")
        return "TVG must run before Angle-Range Correction";
    const auto normalizationRank = [](const std::string& id) {
        if (id == "gain_normalize") return 0;
        if (id == "beam_pattern") return 1;
        if (id == "arn") return 2;
        if (id == "destripe") return 3;
        return -1;
    };
    const int upstream_rank = normalizationRank(upstream.typeId());
    const int downstream_rank = normalizationRank(downstream.typeId());
    if (upstream_rank >= 0 && downstream_rank >= 0
            && downstream_rank < upstream_rank)
        return downstream.label() + " must run before " + upstream.label();
    const auto before = processingContractFor(upstream.typeId());
    const auto after = processingContractFor(downstream.typeId());
    if (after.stage == ProcessingStage::Merge
            || after.stage == ProcessingStage::Output)
        return {};
    if (static_cast<unsigned>(after.stage) < static_cast<unsigned>(before.stage))
        return downstream.label() + " cannot run after " + upstream.label()
             + " because it belongs to an earlier processing stage";
    return {};
}

} // namespace dolphin::pipeline
