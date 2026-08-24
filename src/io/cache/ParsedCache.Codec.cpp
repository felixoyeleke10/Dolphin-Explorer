#include "ParsedCache_p.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <variant>

namespace dolphin::io::detail_cache {
namespace {

void copyFixedString(char* dest, size_t dest_size, const std::string& value)
{
    if (dest_size == 0) return;
    std::memset(dest, 0, dest_size);
    const size_t count = std::min(dest_size - 1, value.size());
    if (count > 0) std::memcpy(dest, value.data(), count);
}

std::string readFixedString(const char* text, size_t text_size)
{
    return std::string(text, strnlen(text, text_size));
}

core::SpatialRefKind spatialRefKindForRecord(const core::SpatialRef& file_ref,
                                             bool is_projected)
{
    if (file_ref.kind != core::SpatialRefKind::Unknown) return file_ref.kind;
    return is_projected ? core::SpatialRefKind::Projected : core::SpatialRefKind::Unknown;
}

} // namespace

void setFileHeaderMetadata(CacheFileHeader& header, const FormatMeta& meta)
{
    header.coord_kind  = static_cast<uint8_t>(meta.coordinate_ref.kind);
    header.coord_exact = meta.coordinate_ref.exact ? 1u : 0u;
    copyFixedString(header.coord_id, sizeof(header.coord_id), meta.coordinate_ref.id);
    header.artifact_role    = meta.artifact_role;
    header.frequency_hz     = meta.frequency_hz;
    header.low_frequency_hz = meta.low_frequency_hz;
    copyFixedString(header.vessel_name, sizeof(header.vessel_name), meta.vessel_name);
    copyFixedString(header.survey_name, sizeof(header.survey_name), meta.survey_name);
    copyFixedString(header.sonar_name, sizeof(header.sonar_name), meta.sonar_name);
}

void loadFileHeaderMetadata(const CacheFileHeader& header, FormatMeta& meta)
{
    core::SpatialRef ref;
    ref.id    = readFixedString(header.coord_id, sizeof(header.coord_id));
    ref.kind  = static_cast<core::SpatialRefKind>(header.coord_kind);
    ref.exact = header.coord_exact != 0;
    if (ref.kind == core::SpatialRefKind::Unknown && !ref.id.empty())
        ref = geo::spatialRefFromId(ref.id);

    meta.coordinate_ref   = std::move(ref);
    meta.artifact_role    = header.artifact_role;
    meta.frequency_hz     = header.frequency_hz;
    meta.low_frequency_hz = header.low_frequency_hz;
    meta.vessel_name = readFixedString(header.vessel_name, sizeof(header.vessel_name));
    meta.survey_name = readFixedString(header.survey_name, sizeof(header.survey_name));
    meta.sonar_name  = readFixedString(header.sonar_name, sizeof(header.sonar_name));
}

core::SpatialRef spatialRefForRecord(const core::SpatialRef& file_ref, bool is_projected)
{
    if (!file_ref.empty()) return file_ref;
    if (is_projected) return core::makeUnknownProjectedSpatialRef();
    return {};
}

CacheNavHeader toCacheNav(const core::NavPoint& nav)
{
    CacheNavHeader out;
    out.lat = nav.lat; out.lon = nav.lon; out.heading_deg = nav.heading_deg;
    out.speed_kn = nav.speed_kn; out.altitude_m = nav.altitude_m;
    out.pitch_deg = nav.pitch_deg; out.roll_deg = nav.roll_deg; out.heave_m = nav.heave_m;
    out.timestamp = nav.timestamp; out.valid = nav.valid ? 1u : 0u;
    out.is_projected = nav.is_projected ? 1u : 0u;
    return out;
}

core::NavPoint fromCacheNav(const CacheNavHeader& nav)
{
    core::NavPoint out;
    out.lat = nav.lat; out.lon = nav.lon; out.heading_deg = nav.heading_deg;
    out.speed_kn = nav.speed_kn; out.altitude_m = nav.altitude_m;
    out.pitch_deg = nav.pitch_deg; out.roll_deg = nav.roll_deg; out.heave_m = nav.heave_m;
    out.timestamp = nav.timestamp; out.valid = nav.valid != 0;
    out.is_projected = nav.is_projected != 0;
    return out;
}

void artifactLatLon(const core::Artifact& artifact, double& lat, double& lon)
{
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, core::RasterGrid>) {
            lat = 0.0; lon = 0.0;
        } else {
            lat = value.nav.lat; lon = value.nav.lon;
        }
    }, artifact);
}

uint32_t payloadSize(const core::Artifact& artifact)
{
    return std::visit([](const auto& value) -> uint32_t {
        using T = std::decay_t<decltype(value)>;
        uint64_t total = 0;
        if constexpr (std::is_same_v<T, core::SidescanPing>)
            total = sizeof(CacheSidescanPayloadHeader) + value.samples.size() * sizeof(CacheSidescanSample);
        else if constexpr (std::is_same_v<T, core::SubBottomTrace>)
            total = sizeof(CacheSubBottomPayloadHeader) + value.samples.size() * sizeof(float);
        else if constexpr (std::is_same_v<T, core::MagSample>)
            total = sizeof(CacheMagPayloadHeader);
        return total <= UINT32_MAX ? static_cast<uint32_t>(total) : 0u;
    }, artifact);
}

bool writePayload(FILE* file, const core::Artifact& artifact)
{
    return std::visit([&](const auto& value) -> bool {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, core::SidescanPing>) {
            CacheSidescanPayloadHeader hdr;
            hdr.channel = value.channel == core::SidescanChannel::Starboard ? 1u : 0u;
            hdr.ping_number = value.ping_number; hdr.frequency_hz = value.frequency_hz;
            hdr.sample_rate_hz = value.sample_rate_hz; hdr.slant_range_m = value.slant_range_m;
            hdr.sound_velocity_ms = value.sound_velocity_ms; hdr.tow_depth_m = value.tow_depth_m;
            hdr.blanking_m = value.blanking_m; hdr.volt_scale = value.volt_scale;
            hdr.gain_code = value.gain_code; hdr.initial_gain_code = value.initial_gain_code;
            hdr.bandwidth_hz = value.bandwidth_hz; hdr.kp_m = value.kp_m;
            hdr.layback_m = value.layback_m; hdr.cable_out_m = value.cable_out_m;
            hdr.fish_delta_x_m = value.fish_delta_x_m; hdr.fish_delta_y_m = value.fish_delta_y_m;
            hdr.nav = toCacheNav(value.nav); hdr.sample_count = static_cast<uint32_t>(value.samples.size());
            hdr.bottom_pick_range_m = value.bottom_pick.range_m;
            hdr.bottom_pick_confidence = value.bottom_pick.confidence;
            hdr.bottom_pick_source = value.bottom_pick.source; hdr.correction_flags = value.correction_flags;
            hdr.qc_flags = value.qc_flags; hdr.fish_lat = value.nav.fish_lat; hdr.fish_lon = value.nav.fish_lon;
            hdr.vessel_lat = value.nav.vessel_lat; hdr.vessel_lon = value.nav.vessel_lon;
            hdr.sensor_heading_deg = value.nav.sensor_heading_deg; hdr.ship_heading_deg = value.nav.ship_heading_deg;
            hdr.fish_nav_valid = value.nav.fish_nav_valid ? 1u : 0u;
            hdr.vessel_nav_valid = value.nav.vessel_nav_valid ? 1u : 0u;
            if (!writePod(file, hdr)) return false;
            for (const auto& sample : value.samples) {
                CacheSidescanSample out{sample.amplitude, sample.range_m};
                if (!writePod(file, out)) return false;
            }
            return true;
        } else if constexpr (std::is_same_v<T, core::SubBottomTrace>) {
            CacheSubBottomPayloadHeader hdr;
            hdr.frequency_hz = value.frequency_hz; hdr.sample_rate_hz = value.sample_rate_hz;
            hdr.tow_depth_m = value.tow_depth_m; hdr.two_way_time_s = value.two_way_time_s;
            hdr.nav = toCacheNav(value.nav); hdr.sample_count = static_cast<uint32_t>(value.samples.size());
            hdr.bottom_sample_idx = value.bottom_sample_idx; hdr.correction_flags = value.correction_flags;
            if (!writePod(file, hdr)) return false;
            return value.samples.empty() || std::fwrite(value.samples.data(), sizeof(float), value.samples.size(), file) == value.samples.size();
        } else if constexpr (std::is_same_v<T, core::MagSample>) {
            CacheMagPayloadHeader hdr;
            hdr.nav = toCacheNav(value.nav); hdr.total_nT = value.total_nT;
            hdr.x_nT = value.x_nT; hdr.y_nT = value.y_nT; hdr.z_nT = value.z_nT;
            hdr.diurnal_nT = value.diurnal_nT; hdr.igrf_nT = value.igrf_nT; hdr.residual_nT = value.residual_nT;
            return writePod(file, hdr);
        }
        return false;
    }, artifact);
}

bool readRecordHeader(FILE* file, CacheRecordHeader& header)
{
    return readPod(file, header) && sameMagic(header.magic, kRecordMagic);
}

void appendIndexEntry(core::ArtifactIndex& index, const CacheRecordHeader& header,
                      uint64_t offset, const core::SpatialRef& file_ref)
{
    core::ArtifactIndexEntry entry;
    entry.artifact_id = header.artifact_id;
    entry.type = static_cast<core::ArtifactType>(header.type);
    entry.timestamp_us = header.timestamp_us; entry.file_offset = offset;
    entry.subrecord_offset = 0;
    entry.byte_length = static_cast<uint32_t>(sizeof(CacheRecordHeader)) + header.payload_size;
    entry.lat = header.lat; entry.lon = header.lon; entry.ping_number = header.ping_number;
    entry.frequency_hz = header.frequency_hz;
    entry.spatial_ref_kind = spatialRefKindForRecord(file_ref, header.is_projected != 0);
    entry.is_projected = header.is_projected != 0;
    if (!entry.is_projected && (std::fabs(entry.lon) > 180.0 || std::fabs(entry.lat) > 90.0)) {
        entry.is_projected = true;
        entry.spatial_ref_kind = core::SpatialRefKind::Projected;
    }
    index.entries.push_back(entry);
}

} // namespace dolphin::io::detail_cache
