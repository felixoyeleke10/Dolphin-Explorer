// JsfReader.Index.cpp — JsfReader::buildIndex.
#include "io/jsf/JsfReader_p.h"

#include <cmath>

namespace dolphin::io {

using namespace detail_jsf;

core::ArtifactIndex JsfReader::buildIndex(ProgressFn progress)
{
    if (!m_file) return {};

    core::ArtifactIndex index;
    index.source_id = m_path;

    if (!detail::seekAbs(m_file, 0))
        return {};

    uint64_t artifact_id     = 0;
    double   first_ts        = -1.0;
    double   last_ts         =  0.0;
    bool     any_projected   = false;

    while (true) {
        uint64_t offset = 0;
        if (!detail::tellFile(m_file, offset)) break;
        if (offset >= m_fileSize) break;

        if (progress && m_fileSize > 0)
            progress(static_cast<float>(offset) / static_cast<float>(m_fileSize));

        JsfPacketHeader pkt{};
        if (fread(&pkt, sizeof(pkt), 1, m_file) != 1) break;

        if (pkt.marker != JSF_MARKER) {
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }

        if (pkt.size == 0) continue;
        if (pkt.size > kMaxRecordSz) {
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }
        if (offset + sizeof(JsfPacketHeader) + pkt.size > m_fileSize) break;

        if (pkt.type == MSG_SONAR && pkt.size >= kPingHdrSize) {
            JsfSonarPingHeader ph{};
            if (fread(&ph, sizeof(ph), 1, m_file) != 1) break;

            if (ph.time_sec > 946684800u) {
                int64_t ts_us = pingTimestampUs(ph);
                double  ts_s  = ts_us * 1e-6;
                if (first_ts < 0.0) first_ts = ts_s;
                last_ts = ts_s;

                const bool sensor_ok = hasUsableSensorCoord(ph.sensor_lat, ph.sensor_lon);
                double lat = sensor_ok ? static_cast<double>(ph.sensor_lat) : ph.ship_lat;
                double lon = sensor_ok ? static_cast<double>(ph.sensor_lon) : ph.ship_lon;

                core::ArtifactIndexEntry entry;
                entry.artifact_id      = artifact_id++;
                entry.type             = classifySubsystem(pkt.subsystem);
                entry.timestamp_us     = ts_us;
                entry.file_offset      = offset;
                entry.byte_length      = static_cast<uint32_t>(sizeof(JsfPacketHeader))
                                       + pkt.size;
                entry.frequency_hz     = static_cast<float>(ph.frequency_hz);
                const bool projected = std::isfinite(lat) && std::isfinite(lon)
                    && (std::abs(lat) > 90.0 || std::abs(lon) > 180.0);
                if (projected) any_projected = true;

                entry.lat              = lat;
                entry.lon              = lon;
                entry.spatial_ref_kind = projected
                    ? core::SpatialRefKind::Projected
                    : core::SpatialRefKind::Geographic;
                entry.is_projected     = projected;
                index.entries.push_back(entry);
            }
        }

        if (!detail::seekAbs(
                m_file,
                offset + sizeof(JsfPacketHeader) + static_cast<uint64_t>(pkt.size))) {
            break;
        }
    }

    if (any_projected)
        m_meta.coordinate_ref = core::makeUnknownProjectedSpatialRef("PROJECTED:JSF_MAGNITUDE");

    m_meta.artifact_count        = static_cast<uint32_t>(index.entries.size());
    m_meta.sidescan_ping_count   = static_cast<uint32_t>(index.byType(core::ArtifactType::Sidescan).size());
    m_meta.subbottom_trace_count = static_cast<uint32_t>(index.byType(core::ArtifactType::SubBottom).size());
    m_meta.start_time = first_ts;
    m_meta.end_time   = last_ts;

    // Derive frequency metadata from per-ping entries (same logic as XTF buildIndex).
    // JSF has no file-level channel-info block, so this is the only source.
    // Also runs when frequency_hz is set but low_frequency_hz is 0 so a second band
    // reported only by individual pings is not missed.
    if (m_meta.frequency_hz == 0.f || m_meta.low_frequency_hz == 0.f) {
        std::vector<float> sss_freqs;
        for (const auto& e : index.entries) {
            if (e.type == core::ArtifactType::Sidescan && e.frequency_hz > 0.f) {
                bool found = false;
                for (float f : sss_freqs)
                    if (std::fabs(f - e.frequency_hz) < 1.f) { found = true; break; }
                if (!found) sss_freqs.push_back(e.frequency_hz);
            }
        }
        std::sort(sss_freqs.begin(), sss_freqs.end(), std::greater<float>());
        if (!sss_freqs.empty()) {
            m_meta.frequency_hz = sss_freqs[0];
            if (sss_freqs.size() >= 2)
                m_meta.low_frequency_hz = sss_freqs.back();
        }
    }

    if (m_meta.frequency_hz > 0.f && m_meta.low_frequency_hz == 0.f) {
        for (auto& e : index.entries) {
            if (e.type == core::ArtifactType::Sidescan && e.frequency_hz == 0.f)
                e.frequency_hz = m_meta.frequency_hz;
        }
    }

    return index;
}

} // namespace dolphin::io
