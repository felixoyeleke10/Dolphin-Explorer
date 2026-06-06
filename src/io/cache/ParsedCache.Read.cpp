// ParsedCache.Read.cpp — ParsedCacheReader::readArtifact and writeParsedCache.
#include "io/cache/ParsedCache_p.h"

#include <filesystem>

namespace dolphin::io {

using namespace detail_cache;

std::optional<core::Artifact>
ParsedCacheReader::readArtifact(const core::ArtifactIndexEntry& entry)
{
    if (!m_file) return std::nullopt;
    if (m_cur_pos != entry.file_offset) {
        if (!seekFile(m_file, entry.file_offset)) {
            m_cur_pos = UINT64_MAX;
            return std::nullopt;
        }
    }

    // Mark position unknown before reading so any mid-record failure leaves
    // m_cur_pos = UINT64_MAX, forcing a seek on the next call.
    m_cur_pos = UINT64_MAX;

    CacheRecordHeader header{};
    if (!readRecordHeader(m_file, header)) return std::nullopt;

    switch (static_cast<core::ArtifactType>(header.type)) {
    case core::ArtifactType::Sidescan: {
        if (header.payload_size < sizeof(CacheSidescanPayloadHeader))
            return std::nullopt;
        CacheSidescanPayloadHeader payload{};
        if (!readPod(m_file, payload)) return std::nullopt;
        const uint32_t sample_bytes = header.payload_size
            - static_cast<uint32_t>(sizeof(CacheSidescanPayloadHeader));
        const uint32_t max_samples = sample_bytes / static_cast<uint32_t>(sizeof(CacheSidescanSample));
        if (payload.sample_count > max_samples)
            return std::nullopt;

        core::SidescanPing ping;
        ping.id             = header.artifact_id;
        ping.timestamp_us   = header.timestamp_us;
        ping.ping_number    = payload.ping_number;
        ping.channel        = payload.channel ? core::SidescanChannel::Starboard
                                              : core::SidescanChannel::Port;
        ping.nav            = fromCacheNav(payload.nav);
        ping.nav.spatial_ref = spatialRefForRecord(m_meta.coordinate_ref, ping.nav.is_projected);
        // Cache built before PACKET_NAV interpolation may have nav.valid=false;
        // fall back to the index entry's interpolated coordinates.
        if (!ping.nav.valid && (entry.lat != 0.0 || entry.lon != 0.0)) {
            ping.nav.lat         = entry.lat;
            ping.nav.lon         = entry.lon;
            ping.nav.valid       = true;
            ping.nav.is_projected = entry.is_projected;
            ping.nav.spatial_ref = spatialRefForRecord(m_meta.coordinate_ref, entry.is_projected);
        }
        ping.frequency_hz      = payload.frequency_hz;
        ping.sample_rate_hz    = payload.sample_rate_hz;
        ping.slant_range_m     = payload.slant_range_m;
        ping.sound_velocity_ms = payload.sound_velocity_ms;
        ping.tow_depth_m       = payload.tow_depth_m;
        ping.blanking_m        = payload.blanking_m;
        ping.volt_scale        = payload.volt_scale;
        ping.gain_code         = payload.gain_code;
        ping.initial_gain_code = payload.initial_gain_code;
        ping.bandwidth_hz      = payload.bandwidth_hz;
        ping.kp_m              = payload.kp_m;
        ping.layback_m         = payload.layback_m;
        ping.cable_out_m       = payload.cable_out_m;
        ping.fish_delta_x_m    = payload.fish_delta_x_m;
        ping.fish_delta_y_m    = payload.fish_delta_y_m;
        ping.nav.fish_lat           = payload.fish_lat;
        ping.nav.fish_lon           = payload.fish_lon;
        ping.nav.vessel_lat         = payload.vessel_lat;
        ping.nav.vessel_lon         = payload.vessel_lon;
        ping.nav.sensor_heading_deg = payload.sensor_heading_deg;
        ping.nav.ship_heading_deg   = payload.ship_heading_deg;
        ping.nav.fish_nav_valid     = payload.fish_nav_valid   != 0;
        ping.nav.vessel_nav_valid   = payload.vessel_nav_valid != 0;
        ping.bottom_pick.range_m    = payload.bottom_pick_range_m;
        ping.bottom_pick.confidence = payload.bottom_pick_confidence;
        ping.bottom_pick.source     = payload.bottom_pick_source;
        ping.correction_flags       = payload.correction_flags;
        ping.qc_flags               = payload.qc_flags;
        ping.samples.reserve(payload.sample_count);
        for (uint32_t i = 0; i < payload.sample_count; ++i) {
            CacheSidescanSample sample{};
            if (!readPod(m_file, sample)) return std::nullopt;
            core::SidescanSample out;
            out.amplitude = sample.amplitude;
            out.range_m   = sample.range_m;
            ping.samples.push_back(out);
        }
        m_cur_pos = entry.file_offset + entry.byte_length;
        return ping;
    }
    case core::ArtifactType::SubBottom: {
        // v26 added correction_flags + reserved (8 bytes) to CacheSubBottomPayloadHeader.
        // v25 files have the smaller header; read the old portion and leave correction_flags=0.
        constexpr uint32_t kNewHdrSize = static_cast<uint32_t>(sizeof(CacheSubBottomPayloadHeader));
        constexpr uint32_t kOldHdrSize = kNewHdrSize - 8u;
        const bool use_v25_layout = (m_file_version < 26);
        const uint32_t hdr_size = use_v25_layout ? kOldHdrSize : kNewHdrSize;
        if (header.payload_size < hdr_size)
            return std::nullopt;
        CacheSubBottomPayloadHeader payload{};  // zero-init → correction_flags=0 for v25
        if (use_v25_layout) {
            if (std::fread(&payload, hdr_size, 1, m_file) != 1) return std::nullopt;
        } else {
            if (!readPod(m_file, payload)) return std::nullopt;
        }
        const uint32_t sample_bytes = header.payload_size - hdr_size;
        const uint32_t max_samples = sample_bytes / static_cast<uint32_t>(sizeof(float));
        if (payload.sample_count > max_samples)
            return std::nullopt;

        core::SubBottomTrace trace;
        trace.id             = header.artifact_id;
        trace.timestamp_us   = header.timestamp_us;
        trace.nav            = fromCacheNav(payload.nav);
        trace.nav.spatial_ref = spatialRefForRecord(m_meta.coordinate_ref, trace.nav.is_projected);
        trace.frequency_hz      = payload.frequency_hz;
        trace.sample_rate_hz    = payload.sample_rate_hz;
        trace.tow_depth_m       = payload.tow_depth_m;
        trace.two_way_time_s    = payload.two_way_time_s;
        trace.bottom_sample_idx = payload.bottom_sample_idx;
        trace.correction_flags  = payload.correction_flags;
        trace.samples.resize(payload.sample_count);
        if (!trace.samples.empty()) {
            if (std::fread(trace.samples.data(), sizeof(float),
                    trace.samples.size(), m_file) != trace.samples.size())
                return std::nullopt;
        }
        m_cur_pos = entry.file_offset + entry.byte_length;
        return trace;
    }
    case core::ArtifactType::Magnetometer: {
        if (header.payload_size < sizeof(CacheMagPayloadHeader))
            return std::nullopt;
        CacheMagPayloadHeader payload{};
        if (!readPod(m_file, payload)) return std::nullopt;

        core::MagSample mag;
        mag.id           = header.artifact_id;
        mag.timestamp_us = header.timestamp_us;
        mag.nav          = fromCacheNav(payload.nav);
        mag.nav.spatial_ref = spatialRefForRecord(m_meta.coordinate_ref, mag.nav.is_projected);
        mag.total_nT     = payload.total_nT;
        mag.x_nT         = payload.x_nT;
        mag.y_nT         = payload.y_nT;
        mag.z_nT         = payload.z_nT;
        mag.diurnal_nT   = payload.diurnal_nT;
        mag.igrf_nT      = payload.igrf_nT;
        mag.residual_nT  = payload.residual_nT;
        m_cur_pos = entry.file_offset + entry.byte_length;
        return mag;
    }
    default:
        return std::nullopt;
    }
}

bool writeParsedCache(const std::string& cache_path,
                      const core::ArtifactIndex& source_index,
                      IFormatReader& source_reader,
                      core::ArtifactIndex& cache_index,
                      ProgressFn progress)
{
    cache_index = {};
    cache_index.source_id = source_index.source_id;

    if (cache_path.empty()) return false;

    std::error_code ec;
    const std::filesystem::path out_path(cache_path);
    const auto parent = out_path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    if (ec) return false;

    const std::string temp_path = tempPathFor(cache_path);
    removeIfExists(temp_path);

    FILE* file = nullptr;
#ifdef _WIN32
    fopen_s(&file, temp_path.c_str(), "wb");
#else
    file = std::fopen(temp_path.c_str(), "wb");
#endif
    if (!file) return false;

    auto fail = [&]() -> bool {
        std::fclose(file);
        removeIfExists(temp_path);
        return false;
    };

    const FormatMeta source_meta = source_reader.metadata();

    CacheFileHeader file_header{};
    std::memcpy(file_header.magic, kFileMagic.data(), kFileMagic.size());
    file_header.version = kCacheVersion;
    setFileHeaderMetadata(file_header, source_meta);
    if (!writePod(file, file_header)) return fail();

    const size_t total = source_index.entries.size();
    for (size_t i = 0; i < total; ++i) {
        if (progress && total > 0)
            progress(static_cast<float>(i) / static_cast<float>(total));

        auto artifact = source_reader.readArtifact(source_index.entries[i]);
        if (!artifact) continue;

        const uint32_t psize = payloadSize(*artifact);
        if (psize == 0) continue;

        uint64_t offset = 0;
        if (!tellFile(file, offset)) return fail();

        CacheRecordHeader record{};
        std::memcpy(record.magic, kRecordMagic.data(), kRecordMagic.size());
        record.type         = static_cast<uint8_t>(core::artifactType(*artifact));
        record.payload_size = psize;
        record.artifact_id  = core::artifactId(*artifact);
        record.timestamp_us = core::artifactTimestamp(*artifact);
        artifactLatLon(*artifact, record.lat, record.lon);
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<T, core::RasterGrid>)
                record.is_projected = v.nav.is_projected ? 1u : 0u;
            if constexpr (std::is_same_v<T, core::SidescanPing>) {
                record.ping_number  = v.ping_number;
                record.frequency_hz = v.frequency_hz;
            }
        }, *artifact);

        if (!writePod(file, record)) return fail();
        if (!writePayload(file, *artifact)) return fail();

        appendIndexEntry(cache_index, record, offset, source_meta.coordinate_ref);
    }

    if (progress) progress(1.0f);

    if (std::fclose(file) != 0) {
        removeIfExists(temp_path);
        return false;
    }

    std::filesystem::rename(std::filesystem::path(temp_path), out_path, ec);
    if (ec) {
        removeIfExists(temp_path);
        return false;
    }

    return !cache_index.empty();
}

bool writeArtifactBufferToCache(const std::string& cache_path,
                               const std::vector<core::Artifact>& buffer,
                               const FormatMeta& meta,
                               core::ArtifactIndex& out_index)
{
    out_index = {};

    if (cache_path.empty() || buffer.empty()) return false;

    std::error_code ec;
    const std::filesystem::path out_path(cache_path);
    const auto parent = out_path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    if (ec) return false;

    const std::string temp_path = tempPathFor(cache_path);
    removeIfExists(temp_path);

    FILE* file = nullptr;
#ifdef _WIN32
    fopen_s(&file, temp_path.c_str(), "wb");
#else
    file = std::fopen(temp_path.c_str(), "wb");
#endif
    if (!file) return false;

    auto fail = [&]() -> bool {
        std::fclose(file);
        removeIfExists(temp_path);
        return false;
    };

    CacheFileHeader file_header{};
    std::memcpy(file_header.magic, kFileMagic.data(), kFileMagic.size());
    file_header.version = kCacheVersion;
    setFileHeaderMetadata(file_header, meta);
    if (!writePod(file, file_header)) return fail();

    for (const auto& artifact : buffer) {
        const uint32_t psize = payloadSize(artifact);
        if (psize == 0) continue; // unsupported type (Multibeam, Raster) — skip silently

        uint64_t offset = 0;
        if (!tellFile(file, offset)) return fail();

        CacheRecordHeader record{};
        std::memcpy(record.magic, kRecordMagic.data(), kRecordMagic.size());
        record.type         = static_cast<uint8_t>(core::artifactType(artifact));
        record.payload_size = psize;
        record.artifact_id  = core::artifactId(artifact);
        record.timestamp_us = core::artifactTimestamp(artifact);
        artifactLatLon(artifact, record.lat, record.lon);
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<T, core::RasterGrid>)
                record.is_projected = v.nav.is_projected ? 1u : 0u;
            if constexpr (std::is_same_v<T, core::SidescanPing>) {
                record.ping_number  = v.ping_number;
                record.frequency_hz = v.frequency_hz;
            }
        }, artifact);

        if (!writePod(file, record)) return fail();
        if (!writePayload(file, artifact)) return fail();

        appendIndexEntry(out_index, record, offset, meta.coordinate_ref);
    }

    if (std::fclose(file) != 0) {
        removeIfExists(temp_path);
        return false;
    }

    std::filesystem::rename(std::filesystem::path(temp_path), out_path, ec);
    if (ec) {
        removeIfExists(temp_path);
        return false;
    }

    return !out_index.empty();
}

} // namespace dolphin::io
