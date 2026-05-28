#include "app/contracts/ContractStore.h"

#include <algorithm>
#include <functional>
#include <sstream>

namespace dolphin::app::contracts {

namespace {

std::string payloadFingerprint(const ContractEnvelope& envelope)
{
    return std::visit([&](const auto& payload) -> std::string {
        using T = std::decay_t<decltype(payload)>;
        std::ostringstream oss;
        oss << contractTypeName(envelope.type) << '|';

        if constexpr (std::is_same_v<T, ProcessedSidescanLayer>) {
            oss << payload.layer_id << '|'
                << payload.source_id << '|'
                << payload.artifact_store_path << '|'
                << payload.artifact_store_format << '|'
                << payload.artifact_index.source_id << '|'
                << payload.artifact_index.size() << '|'
                << payload.graph_hash;
        } else if constexpr (std::is_same_v<T, BottomTrackResult>) {
            oss << payload.layer_id << '|'
                << payload.bottom_samples.size() << '|'
                << payload.confidence_mean;
        } else if constexpr (std::is_same_v<T, QCFlags>) {
            oss << payload.layer_id << '|'
                << payload.pass << '|'
                << payload.warnings.size() << '|'
                << payload.failures.size();
        } else if constexpr (std::is_same_v<T, SurveyStatistics>) {
            oss << payload.layer_id << '|'
                << payload.sidescan_ping_count << '|'
                << payload.track_length_m << '|'
                << payload.coverage_m2;
        } else if constexpr (std::is_same_v<T, ReportPackage>) {
            oss << payload.report_id << '|'
                << payload.output_dir << '|'
                << payload.files.size();
        }

        return oss.str();
    }, envelope.payload);
}

std::string stableHash(const std::string& text)
{
    return std::to_string(std::hash<std::string>{}(text));
}

} // namespace

std::string contractTypeName(ContractType type)
{
    switch (type) {
    case ContractType::ProcessedSidescanLayer: return "ProcessedSidescanLayer";
    case ContractType::BottomTrackResult:      return "BottomTrackResult";
    case ContractType::QCFlags:                return "QCFlags";
    case ContractType::SurveyStatistics:       return "SurveyStatistics";
    case ContractType::ReportPackage:          return "ReportPackage";
    }
    return "UnknownContract";
}

void ContractStore::publish(const std::string& producer_worker_id,
                            const std::vector<ContractEnvelope>& outputs)
{
    for (auto envelope : outputs) {
        envelope.producer_worker_id = producer_worker_id;
        if (envelope.content_hash.empty())
            envelope.content_hash = stableHash(payloadFingerprint(envelope));
        m_latest[{envelope.type, envelope.binding_key}] = std::move(envelope);
    }
}

const ContractEnvelope* ContractStore::latest(ContractType type,
                                              const std::string& binding_key) const
{
    auto it = m_latest.find({type, binding_key});
    return it != m_latest.end() ? &it->second : nullptr;
}

std::vector<ContractEnvelope> ContractStore::latestByType(ContractType type) const
{
    std::vector<ContractEnvelope> result;
    for (const auto& [key, envelope] : m_latest) {
        if (key.first == type)
            result.push_back(envelope);
    }
    return result;
}

std::vector<ContractEnvelope> ContractStore::all() const
{
    std::vector<ContractEnvelope> result;
    result.reserve(m_latest.size());
    for (const auto& [_, envelope] : m_latest)
        result.push_back(envelope);
    return result;
}

void ContractStore::clear()
{
    m_latest.clear();
}

std::string ContractStore::hash(const std::vector<ContractEnvelope>& envelopes)
{
    std::vector<std::string> parts;
    parts.reserve(envelopes.size());
    for (const auto& envelope : envelopes) {
        parts.push_back(envelope.producer_worker_id + "|" +
                        contractTypeName(envelope.type) + "|" +
                        envelope.binding_key + "|" +
                        envelope.content_hash);
    }
    std::sort(parts.begin(), parts.end());

    std::ostringstream oss;
    for (const auto& part : parts)
        oss << part << '\n';
    return stableHash(oss.str());
}

} // namespace dolphin::app::contracts
