// ParsedCache.Index.cpp — ParsedCacheReader::buildIndex.
#include "io/cache/ParsedCache_p.h"

namespace dolphin::io {

using namespace detail_cache;

core::ArtifactIndex ParsedCacheReader::buildIndex(ProgressFn progress)
{
    core::ArtifactIndex index;
    if (!m_file) return index;

    index.source_id = m_path;
    if (!seekFile(m_file, sizeof(CacheFileHeader))) return index;

    double first_ts = -1.0;
    double last_ts  = 0.0;
    bool valid = true;
    m_meta.bottom_pick_src_mask  = 0;
    m_meta.correction_flags_seen = 0;

    while (true) {
        uint64_t offset = 0;
        if (!tellFile(m_file, offset)) {
            valid = false;
            break;
        }
        if (offset >= m_fileSize) break;

        if (progress && m_fileSize > 0)
            progress(static_cast<float>(offset) / static_cast<float>(m_fileSize));

        CacheRecordHeader header{};
        if (!readPod(m_file, header)) {
            valid = false;
            break;
        }
        if (!sameMagic(header.magic, kRecordMagic)) {
            valid = false;
            break;
        }

        const uint64_t record_bytes = sizeof(CacheRecordHeader) + header.payload_size;
        if (offset + record_bytes > m_fileSize) {
            valid = false;
            break;
        }

        appendIndexEntry(index, header, offset, m_meta.coordinate_ref);

        // For sidescan records, read the payload header to collect bottom pick source flags.
        if (static_cast<core::ArtifactType>(header.type) == core::ArtifactType::Sidescan
                && header.payload_size >= sizeof(CacheSidescanPayloadHeader)) {
            CacheSidescanPayloadHeader ph{};
            if (readPod(m_file, ph)) {
                if (ph.bottom_pick_source == 1) m_meta.bottom_pick_src_mask |= 0x01;
                else if (ph.bottom_pick_source == 2) m_meta.bottom_pick_src_mask |= 0x02;
                m_meta.correction_flags_seen |= ph.correction_flags;
            }
        }

        // For v26+ subbottom records, accumulate SBP correction flags alongside SSS ones.
        if (static_cast<core::ArtifactType>(header.type) == core::ArtifactType::SubBottom
                && m_file_version >= 26
                && header.payload_size >= sizeof(CacheSubBottomPayloadHeader)) {
            CacheSubBottomPayloadHeader ph{};
            if (readPod(m_file, ph))
                m_meta.correction_flags_seen |= ph.correction_flags;
        }

        const double ts_s = header.timestamp_us * 1e-6;
        if (first_ts < 0.0) first_ts = ts_s;
        last_ts = ts_s;

        if (!seekFile(m_file, offset + record_bytes)) {
            valid = false;
            break;
        }
    }

    // buildIndex() leaves the file pointer at an untracked position;
    // invalidate so readArtifact() always seeks on its first call after this.
    m_cur_pos = UINT64_MAX;

    if (!valid) {
        m_meta = {};
        return {};
    }

    m_meta.artifact_count        = static_cast<uint32_t>(index.entries.size());
    m_meta.sidescan_ping_count   = static_cast<uint32_t>(index.byType(core::ArtifactType::Sidescan).size());
    m_meta.subbottom_trace_count = static_cast<uint32_t>(index.byType(core::ArtifactType::SubBottom).size());
    m_meta.mag_sample_count      = static_cast<uint32_t>(index.byType(core::ArtifactType::Magnetometer).size());
    m_meta.start_time     = first_ts;
    m_meta.end_time       = last_ts;

    return index;
}

} // namespace dolphin::io
