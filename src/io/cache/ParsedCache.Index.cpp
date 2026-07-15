// ParsedCache.Index.cpp — ParsedCacheReader::buildIndex.
#include "io/cache/ParsedCache_p.h"
#ifdef _WIN32
#include <share.h>   // _SH_DENYNO for shared _fsopen
#endif

namespace dolphin::io {

using namespace detail_cache;

core::ArtifactIndex ParsedCacheReader::buildIndex(ProgressFn progress)
{
    return buildIndex(std::move(progress), nullptr);
}

core::ArtifactIndex ParsedCacheReader::buildIndex(ProgressFn progress,
                                                   const std::atomic<bool>* cancel_flag)
{
    core::ArtifactIndex index;
    if (!m_file) return index;

    index.source_id = m_path;

    // Fast path: try to load the pre-built index footer.
    // Always attempted first regardless of callbacks — the footer read is two
    // file seeks plus N×56-byte reads, so it's safe to do even with a progress
    // or cancel callback active.  Falls through to the full scan only when the
    // footer is absent (old files) or corrupted, then writes the footer so the
    // next open is instant.
    if (tryReadIndexFooter(m_file, m_fileSize, m_meta.coordinate_ref, index, m_meta)) {
        if (!index.empty()) {
            index.source_id = m_path;
            // tryReadIndexFooter restores the entries + correction/bottom-pick
            // flags, but NOT the derived metadata counts/times. Recompute them from
            // the (file-ordered) entries so the footer fast path yields the same
            // metadata as a full scan — otherwise artifact_count / *_count /
            // start_time / end_time stay zero on a cached reopen.
            m_meta.artifact_count        = static_cast<uint32_t>(index.entries.size());
            m_meta.sidescan_ping_count   = static_cast<uint32_t>(index.byType(core::ArtifactType::Sidescan).size());
            m_meta.subbottom_trace_count = static_cast<uint32_t>(index.byType(core::ArtifactType::SubBottom).size());
            m_meta.mag_sample_count      = static_cast<uint32_t>(index.byType(core::ArtifactType::Magnetometer).size());
            m_meta.multibeam_ping_count  = static_cast<uint32_t>(index.byType(core::ArtifactType::Multibeam).size());
            m_meta.start_time = index.entries.front().timestamp_us * 1e-6;
            m_meta.end_time   = index.entries.back().timestamp_us  * 1e-6;
            m_cur_pos = UINT64_MAX;
            return index;
        }
    }
    index.entries.clear();

    if (!seekFile(m_file, sizeof(CacheFileHeader))) return index;

    double first_ts = -1.0;
    double last_ts  = 0.0;
    bool valid = true;
    int  last_pct = -1;   // throttle progress to whole-percent steps (avoid event flood)
    m_meta.bottom_pick_src_mask  = 0;
    m_meta.correction_flags_seen = 0;

    while (true) {
        uint64_t offset = 0;
        if (!tellFile(m_file, offset)) {
            valid = false;
            break;
        }
        if (offset >= m_fileSize) break;

        // Throttle to whole-percent changes: the previous per-record call posted
        // ~one queued UI event per record (tens of thousands), flooding and freezing
        // the main thread during a rebuild.
        if (progress && m_fileSize > 0) {
            const int pct = static_cast<int>(100.0 * static_cast<double>(offset)
                                                   / static_cast<double>(m_fileSize));
            if (pct != last_pct) {
                last_pct = pct;
                progress(static_cast<float>(pct) / 100.f);
            }
        }

        // Check for cancellation every 256 records (amortised overhead negligible).
        if (cancel_flag && (index.entries.size() & 0xFF) == 0
                && cancel_flag->load(std::memory_order_relaxed)) {
            m_meta = {};
            m_cur_pos = UINT64_MAX;
            return {};
        }

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
    m_meta.multibeam_ping_count  = static_cast<uint32_t>(index.byType(core::ArtifactType::Multibeam).size());
    m_meta.start_time     = first_ts;
    m_meta.end_time       = last_ts;

    // Append a compact index footer so the next project open takes the fast path.
    // Open in "r+b" (read-write, no truncate) to append without rebuilding.
    // Skip on failure, but roll any partial footer append back so a failed
    // acceleration write can never turn the durable artifact store corrupt.
    // Write even when cancel_flag was provided, as long as the scan was NOT cancelled
    // (a cancellation returns {} early above, so reaching here means the scan finished).
    const bool was_cancelled = cancel_flag && cancel_flag->load(std::memory_order_relaxed);
    if (!was_cancelled && !index.empty()) {
        FILE* wf = nullptr;
#ifdef _WIN32
        // Shared so this append succeeds even though the read handle (m_file) for
        // this same path is still open — otherwise the footer never persists.
        wf = _fsopen(m_path.c_str(), "r+b", _SH_DENYNO);
#else
        wf = std::fopen(m_path.c_str(), "r+b");
#endif
        if (wf) {
            const uint64_t original_size = m_fileSize;
            const bool footer_written = detail::seekEnd(wf)
                && writeIndexFooter(wf, index,
                                    m_meta.correction_flags_seen,
                                    m_meta.bottom_pick_src_mask)
                && std::fflush(wf) == 0;
            const bool close_ok = std::fclose(wf) == 0;
            if (footer_written && close_ok) {
                // Update cached file size only after the complete footer reached
                // the stream successfully.
                m_fileSize += static_cast<uint64_t>(index.entries.size()) * sizeof(CacheIndexEntry)
                            + sizeof(CacheIndexFooter);
            } else {
                std::error_code resize_error;
                std::filesystem::resize_file(
                    std::filesystem::path(m_path), original_size, resize_error);
            }
        }
    }

    return index;
}

core::ArtifactIndex ParsedCacheReader::quickIndex()
{
    core::ArtifactIndex index;
    if (!m_file) return index;
    index.source_id = m_path;
    if (tryReadIndexFooter(m_file, m_fileSize, m_meta.coordinate_ref, index, m_meta)
            && !index.empty()) {
        m_cur_pos = UINT64_MAX;
        return index;
    }
    return {};  // no footer — caller must schedule background buildIndex()
}

} // namespace dolphin::io
