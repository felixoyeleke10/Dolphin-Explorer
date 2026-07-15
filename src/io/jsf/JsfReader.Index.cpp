// JsfReader.Index.cpp — structurally validated Message Type 80 index build.
#include "io/jsf/JsfReader_p.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace dolphin::io {

using namespace detail_jsf;

core::ArtifactIndex JsfReader::buildIndex(ProgressFn progress)
{
    clearDiagnostics();
    if (!m_file) return {};

    core::ArtifactIndex index;
    index.source_id = m_path;

    if (!detail::seekAbs(m_file, 0)) return index;

    uint64_t artifact_id   = 0;
    double   first_ts      = -1.0;
    double   last_ts       = 0.0;
    bool     any_projected = false;
    bool     any_geographic = false;

    while (true) {
        uint64_t offset = 0;
        if (!detail::tellFile(m_file, offset)) break;
        if (offset >= m_fileSize) break;

        if (progress && m_fileSize > 0)
            progress(static_cast<float>(offset) / static_cast<float>(m_fileSize));

        if (m_fileSize - offset < sizeof(JsfPacketHeader)) {
            addDiagnostic(core::ImportDiagnosticSeverity::Error,
                          core::ImportDiagnosticCode::TruncatedRecord,
                          "Truncated JSF message header", offset);
            break;
        }

        JsfPacketHeader pkt{};
        if (std::fread(&pkt, sizeof(pkt), 1, m_file) != 1) {
            addDiagnostic(core::ImportDiagnosticSeverity::Error,
                          core::ImportDiagnosticCode::TruncatedRecord,
                          "Could not read JSF message header", offset);
            break;
        }
        if (pkt.marker != JSF_MARKER) {
            addDiagnostic(core::ImportDiagnosticSeverity::Error,
                          core::ImportDiagnosticCode::BadPacketMagic,
                          "Invalid JSF message marker", offset);
            break;
        }
        if (pkt.size > kMaxRecordSz) {
            addDiagnostic(core::ImportDiagnosticSeverity::Error,
                          core::ImportDiagnosticCode::ImplausibleRecordSize,
                          "JSF message exceeds the 64 MiB safety limit", offset);
            break;
        }

        const uint64_t body_start = offset + sizeof(JsfPacketHeader);
        if (static_cast<uint64_t>(pkt.size) > m_fileSize - body_start) {
            addDiagnostic(core::ImportDiagnosticSeverity::Error,
                          core::ImportDiagnosticCode::TruncatedRecord,
                          "JSF message body extends beyond the file", offset);
            break;
        }
        const uint64_t next = body_start + pkt.size;

        if (pkt.type == MSG_SONAR) {
            if (pkt.size < kPingHdrSize) {
                addDiagnostic(core::ImportDiagnosticSeverity::Warning,
                              core::ImportDiagnosticCode::TruncatedRecord,
                              "Skipped JSF sonar message shorter than 240 bytes", offset);
            } else {
                JsfSonarPingHeader ph{};
                if (std::fread(&ph, sizeof(ph), 1, m_file) != 1) {
                    addDiagnostic(core::ImportDiagnosticSeverity::Error,
                                  core::ImportDiagnosticCode::TruncatedRecord,
                                  "Could not read JSF sonar trace header", body_start);
                    break;
                }

                JsfSampleLayout layout;
                std::string layout_error;
                if (!supportsSubsystem(pkt.subsystem)) {
                    addDiagnostic(core::ImportDiagnosticSeverity::Warning,
                        core::ImportDiagnosticCode::UnsupportedChannelType,
                        "Skipped unsupported JSF sonar subsystem "
                            + std::to_string(pkt.subsystem),
                        offset, pingTimestampUs(ph), pingNumber(ph));
                } else if (!sampleLayout(
                               ph, pkt.version, pkt.size, layout, &layout_error)) {
                    const bool unsupported = componentsPerSample(dataFormat(ph)) == 0;
                    addDiagnostic(core::ImportDiagnosticSeverity::Warning,
                        unsupported
                            ? core::ImportDiagnosticCode::UnsupportedSampleEncoding
                            : core::ImportDiagnosticCode::TruncatedRecord,
                        "Skipped JSF sonar message: " + layout_error, offset,
                        pingTimestampUs(ph), pingNumber(ph));
                } else {
                    const int64_t ts_us = pingTimestampUs(ph);
                    const double ts_s = ts_us * 1e-6;
                    if (first_ts < 0.0) first_ts = ts_s;
                    last_ts = ts_s;

                    const JsfCoordinate pos = coordinate(ph);
                    any_projected  = any_projected  || (pos.valid && pos.projected);
                    any_geographic = any_geographic || (pos.valid && !pos.projected);

                    core::ArtifactIndexEntry entry;
                    entry.artifact_id      = artifact_id++;
                    entry.type             = classifySubsystem(pkt.subsystem);
                    entry.timestamp_us     = ts_us;
                    entry.file_offset      = offset;
                    entry.byte_length      = static_cast<uint32_t>(sizeof(JsfPacketHeader))
                                           + pkt.size;
                    entry.frequency_hz     = static_cast<float>(frequencyHz(ph, pkt.version));
                    entry.ping_number      = pingNumber(ph);
                    entry.lat              = pos.lat;
                    entry.lon              = pos.lon;
                    entry.spatial_ref_kind = !pos.valid
                        ? core::SpatialRefKind::Unknown
                        : (pos.projected ? core::SpatialRefKind::Projected
                                         : core::SpatialRefKind::Geographic);
                    entry.is_projected = pos.valid && pos.projected;
                    index.entries.push_back(entry);

                    if (!pos.valid) {
                        addDiagnostic(core::ImportDiagnosticSeverity::Warning,
                                      core::ImportDiagnosticCode::MissingNavigation,
                                      "JSF ping has no usable position", offset,
                                      ts_us, pingNumber(ph));
                    }
                }
            }
        }

        if (!detail::seekAbs(m_file, next)) break;
    }

    if (progress) progress(1.0f);

    if (any_projected) {
        m_meta.coordinate_ref = core::makeUnknownProjectedSpatialRef("PROJECTED:JSF");
        if (any_geographic) {
            addDiagnostic(core::ImportDiagnosticSeverity::Warning,
                          core::ImportDiagnosticCode::CoordinateEncodingAmbiguous,
                          "JSF contains both projected and geographic positions");
        }
    } else if (any_geographic) {
        m_meta.coordinate_ref = core::makeWgs84SpatialRef();
    }

    m_meta.artifact_count        = static_cast<uint32_t>(index.entries.size());
    m_meta.sidescan_ping_count   = static_cast<uint32_t>(
        index.byType(core::ArtifactType::Sidescan).size());
    m_meta.subbottom_trace_count = static_cast<uint32_t>(
        index.byType(core::ArtifactType::SubBottom).size());
    m_meta.start_time = first_ts < 0.0 ? 0.0 : first_ts;
    m_meta.end_time   = last_ts;

    std::vector<float> sss_freqs;
    for (const auto& e : index.entries) {
        if (e.type != core::ArtifactType::Sidescan || e.frequency_hz <= 0.f) continue;
        const bool found = std::any_of(sss_freqs.begin(), sss_freqs.end(),
            [&](float f) { return std::fabs(f - e.frequency_hz) < 1.f; });
        if (!found) sss_freqs.push_back(e.frequency_hz);
    }
    std::sort(sss_freqs.begin(), sss_freqs.end(), std::greater<float>());
    if (!sss_freqs.empty()) {
        m_meta.frequency_hz = sss_freqs.front();
        if (sss_freqs.size() >= 2) m_meta.low_frequency_hz = sss_freqs.back();
    }

    return index;
}

} // namespace dolphin::io
