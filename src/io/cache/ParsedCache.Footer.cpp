#include "io/cache/ParsedCache_p.h"

#include <filesystem>
#include <system_error>

namespace dolphin::io::detail_cache {

void removeIfExists(const std::string& path)
{
    std::error_code error;
    std::filesystem::remove(std::filesystem::path(path), error);
}

bool writeIndexFooter(FILE* file, const core::ArtifactIndex& index,
                      uint32_t correction_flags_seen,
                      uint8_t bottom_pick_src_mask)
{
    for (const auto& entry : index.entries) {
        CacheIndexEntry stored{};
        stored.artifact_id = entry.artifact_id;
        stored.timestamp_us = entry.timestamp_us;
        stored.file_offset = entry.file_offset;
        stored.byte_length = entry.byte_length;
        stored.frequency_hz = entry.frequency_hz;
        stored.ping_number = entry.ping_number;
        stored.lat = entry.lat;
        stored.lon = entry.lon;
        stored.type = static_cast<uint8_t>(entry.type);
        stored.is_projected = entry.is_projected ? 1u : 0u;
        stored.spatial_ref_kind = static_cast<uint8_t>(entry.spatial_ref_kind);
        if (!writePod(file, stored)) return false;
    }
    CacheIndexFooter footer{};
    footer.entry_count = static_cast<uint32_t>(index.entries.size());
    footer.correction_flags_seen = correction_flags_seen;
    footer.bottom_pick_src_mask = bottom_pick_src_mask;
    footer.footer_version = kIndexFooterVersion;
    std::memcpy(footer.magic, kIndexFooterMagic.data(), kIndexFooterMagic.size());
    return writePod(file, footer);
}

bool tryReadIndexFooter(FILE* file, uint64_t file_size,
                        const core::SpatialRef& file_ref,
                        core::ArtifactIndex& index, FormatMeta& meta)
{
    constexpr uint64_t footer_size = sizeof(CacheIndexFooter);
    constexpr uint64_t entry_size = sizeof(CacheIndexEntry);
    if (file_size < sizeof(CacheFileHeader) + footer_size
            || !seekFile(file, file_size - footer_size)) return false;
    CacheIndexFooter footer{};
    if (!readPod(file, footer) || !sameMagic(footer.magic, kIndexFooterMagic)
            || footer.footer_version != kIndexFooterVersion) return false;

    const uint64_t block_size = static_cast<uint64_t>(footer.entry_count) * entry_size;
    if (block_size > file_size - footer_size) return false;
    const uint64_t block_start = file_size - footer_size - block_size;
    if (block_start < sizeof(CacheFileHeader)
            || block_start + block_size + footer_size != file_size
            || !seekFile(file, block_start)) return false;

    index.entries.reserve(footer.entry_count);
    uint64_t previous_end = sizeof(CacheFileHeader);
    for (uint32_t i = 0; i < footer.entry_count; ++i) {
        CacheIndexEntry stored{};
        if (!readPod(file, stored)
                || stored.file_offset < sizeof(CacheFileHeader)
                || stored.byte_length < sizeof(CacheRecordHeader)
                || stored.file_offset > block_start
                || static_cast<uint64_t>(stored.byte_length)
                    > block_start - stored.file_offset
                || stored.file_offset < previous_end) {
            index.entries.clear();
            return false;
        }
        previous_end = stored.file_offset + stored.byte_length;
        core::ArtifactIndexEntry entry{};
        entry.artifact_id = stored.artifact_id;
        entry.timestamp_us = stored.timestamp_us;
        entry.file_offset = stored.file_offset;
        entry.byte_length = stored.byte_length;
        entry.frequency_hz = stored.frequency_hz;
        entry.ping_number = stored.ping_number;
        entry.lat = stored.lat;
        entry.lon = stored.lon;
        entry.type = static_cast<core::ArtifactType>(stored.type);
        entry.is_projected = stored.is_projected != 0;
        entry.spatial_ref_kind = static_cast<core::SpatialRefKind>(
            stored.spatial_ref_kind);
        if (!entry.is_projected
                && (std::fabs(entry.lon) > 180.0 || std::fabs(entry.lat) > 90.0)) {
            entry.is_projected = true;
            entry.spatial_ref_kind = core::SpatialRefKind::Projected;
        }
        (void)file_ref;
        index.entries.push_back(entry);
    }
    meta.correction_flags_seen = footer.correction_flags_seen;
    meta.bottom_pick_src_mask = footer.bottom_pick_src_mask;
    return true;
}

} // namespace dolphin::io::detail_cache
