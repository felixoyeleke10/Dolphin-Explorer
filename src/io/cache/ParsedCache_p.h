#pragma once
// ParsedCache_p.h — binary structs and helpers shared across ParsedCache TUs.
//
// -- Supported artifact types --------------------------------------------------
// The cache stores three modalities:
//
//   ArtifactType::Sidescan     → CacheSidescanPayloadHeader + CacheSidescanSample[]
//   ArtifactType::SubBottom    → CacheSubBottomPayloadHeader + float[]
//   ArtifactType::Magnetometer → CacheMagPayloadHeader (no samples array)
//
// Unsupported types (Multibeam, Raster) return payloadSize() == 0 and are
// silently skipped by writePayload().  The record is not written to disk.
// Readers will never see an entry for these types in the cached index.
// If you add a new modality, add a payload struct, bump kCacheVersion, and
// handle the new type in payloadSize(), writePayload(), and ParsedCache.Read.cpp.
#include "io/FileIo.h"
#include "io/cache/ParsedCache.h"
#include "geo/GeoUtils.h"
#include "core/Artifact.h"
#include "core/NavPoint.h"
#include "core/SpatialRef.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <variant>
#include <type_traits>

namespace dolphin::io::detail_cache {

inline constexpr std::array<char, 8> kFileMagic        = {'D', 'P', 'C', 'A', 'C', 'H', 'E', '1'};
inline constexpr std::array<char, 4> kRecordMagic      = {'D', 'P', 'R', '1'};
inline constexpr std::array<char, 8> kIndexFooterMagic = {'D', 'P', 'I', 'D', 'X', '1', '\0', '\0'};
inline constexpr uint32_t kCacheVersion         = 26;  // current write version
inline constexpr uint32_t kMinAcceptableVersion = 25;  // v25 SBP lacks correction_flags; read as 0
inline constexpr uint8_t  kIndexFooterVersion   = 1;

#pragma pack(push, 1)
struct CacheFileHeader {
    char     magic[8];
    uint32_t version;
    uint8_t  coord_kind = 0;
    uint8_t  coord_exact = 0;
    uint16_t reserved = 0;
    float    frequency_hz     = 0.0f;
    float    low_frequency_hz = 0.0f;
    char     coord_id[64] = {};
    char     vessel_name[64] = {};
    char     survey_name[64] = {};
    char     sonar_name[64] = {};
};

struct CacheRecordHeader {
    char     magic[4];
    uint8_t  type = 0;
    uint8_t  is_projected = 0;
    uint8_t  reserved[2] = {};
    uint32_t payload_size = 0;
    uint64_t artifact_id = 0;
    int64_t  timestamp_us = 0;
    double   lat = 0.0;
    double   lon = 0.0;
    uint32_t ping_number = 0;
    float    frequency_hz = 0.f; // v23: ping centre frequency; 0 = unknown
    uint32_t reserved2 = 0;
};

struct CacheNavHeader {
    double  lat = 0.0;
    double  lon = 0.0;
    float   heading_deg = 0.0f;
    float   speed_kn = 0.0f;
    float   altitude_m = 0.0f;
    float   pitch_deg = 0.0f;
    float   roll_deg = 0.0f;
    float   heave_m = 0.0f;
    double  timestamp = 0.0;
    uint8_t valid = 0;
    uint8_t is_projected = 0;
    uint8_t reserved[6] = {};
};

struct CacheSidescanPayloadHeader {
    uint8_t  channel = 0;
    uint8_t  reserved[3] = {};
    uint32_t ping_number = 0;
    float    frequency_hz = 0.0f;
    float    sample_rate_hz = 0.0f;
    float    slant_range_m = 0.0f;
    float    sound_velocity_ms = 0.0f;
    float    tow_depth_m = 0.0f;
    float    blanking_m = 0.0f;
    float    volt_scale = 1.0f;
    uint16_t gain_code = 0;
    uint16_t initial_gain_code = 0;
    uint32_t bandwidth_hz = 0;
    float    kp_m = 0.0f;
    float    layback_m = 0.0f;
    float    cable_out_m = 0.0f;
    float    fish_delta_x_m = 0.0f;
    float    fish_delta_y_m = 0.0f;
    CacheNavHeader nav;
    uint32_t sample_count = 0;
    // v22 — bottom detection result + processing state
    float    bottom_pick_range_m    = -1.0f;  // -1 = no detection
    float    bottom_pick_confidence =  0.0f;
    uint8_t  bottom_pick_source     =  0;     // 0=none 1=auto 2=user
    uint32_t correction_flags       =  0;     // bitmask of CorrectionFlag
    uint8_t  qc_flags               =  0;     // bitmask of QcFlag
    uint8_t  reserved2[2]           = {};
    // v24 — extended nav sources: fish/vessel GPS and raw heading fields
    // Absent in older caches (version < 24); resolveSssPosition/buildHeadingTable
    // fall back to nav.lat/lon and nav.heading_deg for Auto mode when these are zero.
    double   fish_lat               = 0.0;
    double   fish_lon               = 0.0;
    double   vessel_lat             = 0.0;
    double   vessel_lon             = 0.0;
    float    sensor_heading_deg     = 0.0f;
    float    ship_heading_deg       = 0.0f;
    uint8_t  fish_nav_valid         = 0;
    uint8_t  vessel_nav_valid       = 0;
    uint8_t  reserved3[6]           = {};
};

struct CacheSidescanSample {
    uint16_t amplitude = 0;
    float    range_m = 0.0f;
};

struct CacheSubBottomPayloadHeader {
    float    frequency_hz = 0.0f;
    float    sample_rate_hz = 0.0f;
    float    tow_depth_m = 0.0f;
    float    two_way_time_s = 0.0f;
    CacheNavHeader nav;
    uint32_t sample_count = 0;
    int32_t  bottom_sample_idx = -1;  // v25: seabed first-return index; -1 = not detected
    uint32_t correction_flags  =  0;  // v26: bitmask of SbpCorrectionFlag; 0 = no baked corrections
    uint32_t reserved          =  0;
};

struct CacheMagPayloadHeader {
    CacheNavHeader nav;
    float total_nT = 0.0f;
    float x_nT = 0.0f;
    float y_nT = 0.0f;
    float z_nT = 0.0f;
    float diurnal_nT = 0.0f;
    float igrf_nT = 0.0f;
    float residual_nT = 0.0f;
};

// -- Index footer (appended after all records) ---------------------------------
// Allows O(1) index loading on project open without rescanning the whole file.
// Layout at end of file: [N × CacheIndexEntry] [CacheIndexFooter]
// Detection: read sizeof(CacheIndexFooter) from fileEnd, check magic field.

struct CacheIndexEntry {
    uint64_t artifact_id;
    int64_t  timestamp_us;
    uint64_t file_offset;
    uint32_t byte_length;
    float    frequency_hz;
    uint32_t ping_number;
    double   lat;
    double   lon;
    uint8_t  type;
    uint8_t  is_projected;
    uint8_t  spatial_ref_kind;
    uint8_t  reserved;
};  // 56 bytes — deliberately no padding needed with this field ordering

struct CacheIndexFooter {
    uint32_t entry_count;
    uint32_t correction_flags_seen;
    uint8_t  bottom_pick_src_mask;
    uint8_t  footer_version;  // = kIndexFooterVersion
    uint8_t  reserved[2];
    char     magic[8];  // kIndexFooterMagic — kept last for easy detection
};  // 20 bytes
#pragma pack(pop)

template <typename T>
static inline bool readPod(FILE* file, T& value)
{
    return std::fread(&value, sizeof(T), 1, file) == 1;
}

template <typename T>
static inline bool writePod(FILE* file, const T& value)
{
    return std::fwrite(&value, sizeof(T), 1, file) == 1;
}

static inline bool tellFile(FILE* file, uint64_t& offset)
{
    return detail::tellFile(file, offset);
}

static inline bool seekFile(FILE* file, uint64_t offset)
{
    return detail::seekAbs(file, offset);
}

static inline bool sameMagic(const char* got, const std::array<char, 8>& expected)
{
    return std::memcmp(got, expected.data(), expected.size()) == 0;
}

static inline bool sameMagic(const char* got, const std::array<char, 4>& expected)
{
    return std::memcmp(got, expected.data(), expected.size()) == 0;
}

static inline void copyFixedString(char* dest, size_t dest_size, const std::string& value)
{
    if (dest_size == 0) return;
    std::memset(dest, 0, dest_size);
    const size_t count = std::min(dest_size - 1, value.size());
    if (count > 0) std::memcpy(dest, value.data(), count);
}

static inline std::string readFixedString(const char* text, size_t text_size)
{
    return std::string(text, strnlen(text, text_size));
}

static inline void setFileSpatialRef(CacheFileHeader& header, const core::SpatialRef& ref)
{
    header.coord_kind  = static_cast<uint8_t>(ref.kind);
    header.coord_exact = ref.exact ? 1u : 0u;
    copyFixedString(header.coord_id, sizeof(header.coord_id), ref.id);
}

static inline void setFileHeaderMetadata(CacheFileHeader& header, const FormatMeta& meta)
{
    setFileSpatialRef(header, meta.coordinate_ref);
    header.frequency_hz     = meta.frequency_hz;
    header.low_frequency_hz = meta.low_frequency_hz;
    copyFixedString(header.vessel_name, sizeof(header.vessel_name), meta.vessel_name);
    copyFixedString(header.survey_name, sizeof(header.survey_name), meta.survey_name);
    copyFixedString(header.sonar_name,  sizeof(header.sonar_name),  meta.sonar_name);
}

static inline core::SpatialRef spatialRefFromFileHeader(const CacheFileHeader& header)
{
    core::SpatialRef ref;
    ref.id    = readFixedString(header.coord_id, sizeof(header.coord_id));
    ref.kind  = static_cast<core::SpatialRefKind>(header.coord_kind);
    ref.exact = header.coord_exact != 0;
    if (ref.kind == core::SpatialRefKind::Unknown && !ref.id.empty())
        ref = geo::spatialRefFromId(ref.id);
    return ref;
}

static inline void loadFileHeaderMetadata(const CacheFileHeader& header, FormatMeta& meta)
{
    meta.coordinate_ref = spatialRefFromFileHeader(header);
    meta.frequency_hz     = header.frequency_hz;
    meta.low_frequency_hz = header.low_frequency_hz;
    meta.vessel_name      = readFixedString(header.vessel_name, sizeof(header.vessel_name));
    meta.survey_name    = readFixedString(header.survey_name, sizeof(header.survey_name));
    meta.sonar_name     = readFixedString(header.sonar_name,  sizeof(header.sonar_name));
}

static inline core::SpatialRef spatialRefForRecord(const core::SpatialRef& file_ref, bool is_projected)
{
    if (!file_ref.empty()) return file_ref;
    if (is_projected) return core::makeUnknownProjectedSpatialRef();
    return {};
}

static inline core::SpatialRefKind spatialRefKindForRecord(const core::SpatialRef& file_ref, bool is_projected)
{
    if (file_ref.kind != core::SpatialRefKind::Unknown)
        return file_ref.kind;
    return is_projected ? core::SpatialRefKind::Projected : core::SpatialRefKind::Unknown;
}

static inline CacheNavHeader toCacheNav(const core::NavPoint& nav)
{
    CacheNavHeader out;
    out.lat          = nav.lat;
    out.lon          = nav.lon;
    out.heading_deg  = nav.heading_deg;
    out.speed_kn     = nav.speed_kn;
    out.altitude_m   = nav.altitude_m;
    out.pitch_deg    = nav.pitch_deg;
    out.roll_deg     = nav.roll_deg;
    out.heave_m      = nav.heave_m;
    out.timestamp    = nav.timestamp;
    out.valid        = nav.valid ? 1u : 0u;
    out.is_projected = nav.is_projected ? 1u : 0u;
    return out;
}

static inline core::NavPoint fromCacheNav(const CacheNavHeader& nav)
{
    core::NavPoint out;
    out.lat          = nav.lat;
    out.lon          = nav.lon;
    out.heading_deg  = nav.heading_deg;
    out.speed_kn     = nav.speed_kn;
    out.altitude_m   = nav.altitude_m;
    out.pitch_deg    = nav.pitch_deg;
    out.roll_deg     = nav.roll_deg;
    out.heave_m      = nav.heave_m;
    out.timestamp    = nav.timestamp;
    out.valid        = nav.valid != 0;
    out.is_projected = nav.is_projected != 0;
    return out;
}

static inline void artifactLatLon(const core::Artifact& artifact, double& lat, double& lon)
{
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, core::RasterGrid>) {
            lat = 0.0; lon = 0.0;
        } else {
            lat = value.nav.lat;
            lon = value.nav.lon;
        }
    }, artifact);
}

static inline uint32_t payloadSize(const core::Artifact& artifact)
{
    return std::visit([](const auto& value) -> uint32_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, core::SidescanPing>) {
            uint64_t total = sizeof(CacheSidescanPayloadHeader)
                           + static_cast<uint64_t>(value.samples.size())
                           * sizeof(CacheSidescanSample);
            return total <= UINT32_MAX ? static_cast<uint32_t>(total) : 0u;
        } else if constexpr (std::is_same_v<T, core::SubBottomTrace>) {
            uint64_t total = sizeof(CacheSubBottomPayloadHeader)
                           + static_cast<uint64_t>(value.samples.size())
                           * sizeof(float);
            return total <= UINT32_MAX ? static_cast<uint32_t>(total) : 0u;
        } else if constexpr (std::is_same_v<T, core::MagSample>) {
            return static_cast<uint32_t>(sizeof(CacheMagPayloadHeader));
        } else {
            return 0u;
        }
    }, artifact);
}

static inline bool writePayload(FILE* file, const core::Artifact& artifact)
{
    return std::visit([&](const auto& value) -> bool {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, core::SidescanPing>) {
            CacheSidescanPayloadHeader hdr;
            hdr.channel           = (value.channel == core::SidescanChannel::Starboard) ? 1u : 0u;
            hdr.ping_number       = value.ping_number;
            hdr.frequency_hz      = value.frequency_hz;
            hdr.sample_rate_hz    = value.sample_rate_hz;
            hdr.slant_range_m     = value.slant_range_m;
            hdr.sound_velocity_ms = value.sound_velocity_ms;
            hdr.tow_depth_m       = value.tow_depth_m;
            hdr.blanking_m        = value.blanking_m;
            hdr.volt_scale        = value.volt_scale;
            hdr.gain_code         = value.gain_code;
            hdr.initial_gain_code = value.initial_gain_code;
            hdr.bandwidth_hz      = value.bandwidth_hz;
            hdr.kp_m              = value.kp_m;
            hdr.layback_m         = value.layback_m;
            hdr.cable_out_m       = value.cable_out_m;
            hdr.fish_delta_x_m    = value.fish_delta_x_m;
            hdr.fish_delta_y_m    = value.fish_delta_y_m;
            hdr.nav               = toCacheNav(value.nav);
            hdr.sample_count      = static_cast<uint32_t>(value.samples.size());
            hdr.bottom_pick_range_m    = value.bottom_pick.range_m;
            hdr.bottom_pick_confidence = value.bottom_pick.confidence;
            hdr.bottom_pick_source     = value.bottom_pick.source;
            hdr.correction_flags       = value.correction_flags;
            hdr.qc_flags               = value.qc_flags;
            hdr.fish_lat               = value.nav.fish_lat;
            hdr.fish_lon               = value.nav.fish_lon;
            hdr.vessel_lat             = value.nav.vessel_lat;
            hdr.vessel_lon             = value.nav.vessel_lon;
            hdr.sensor_heading_deg     = value.nav.sensor_heading_deg;
            hdr.ship_heading_deg       = value.nav.ship_heading_deg;
            hdr.fish_nav_valid         = value.nav.fish_nav_valid   ? 1u : 0u;
            hdr.vessel_nav_valid       = value.nav.vessel_nav_valid ? 1u : 0u;
            if (!writePod(file, hdr)) return false;
            for (const auto& sample : value.samples) {
                CacheSidescanSample out;
                out.amplitude = sample.amplitude;
                out.range_m   = sample.range_m;
                if (!writePod(file, out)) return false;
            }
            return true;
        } else if constexpr (std::is_same_v<T, core::SubBottomTrace>) {
            CacheSubBottomPayloadHeader hdr;
            hdr.frequency_hz   = value.frequency_hz;
            hdr.sample_rate_hz = value.sample_rate_hz;
            hdr.tow_depth_m    = value.tow_depth_m;
            hdr.two_way_time_s = value.two_way_time_s;
            hdr.nav               = toCacheNav(value.nav);
            hdr.sample_count      = static_cast<uint32_t>(value.samples.size());
            hdr.bottom_sample_idx = value.bottom_sample_idx;
            hdr.correction_flags  = value.correction_flags;
            if (!writePod(file, hdr)) return false;
            if (!value.samples.empty()) {
                return std::fwrite(value.samples.data(), sizeof(float),
                    value.samples.size(), file) == value.samples.size();
            }
            return true;
        } else if constexpr (std::is_same_v<T, core::MagSample>) {
            CacheMagPayloadHeader hdr;
            hdr.nav         = toCacheNav(value.nav);
            hdr.total_nT    = value.total_nT;
            hdr.x_nT        = value.x_nT;
            hdr.y_nT        = value.y_nT;
            hdr.z_nT        = value.z_nT;
            hdr.diurnal_nT  = value.diurnal_nT;
            hdr.igrf_nT     = value.igrf_nT;
            hdr.residual_nT = value.residual_nT;
            return writePod(file, hdr);
        } else {
            return false;
        }
    }, artifact);
}

static inline bool readRecordHeader(FILE* file, CacheRecordHeader& header)
{
    if (!readPod(file, header)) return false;
    return sameMagic(header.magic, kRecordMagic);
}

static inline void appendIndexEntry(core::ArtifactIndex& index,
                                    const CacheRecordHeader& header,
                                    uint64_t offset,
                                    const core::SpatialRef& file_ref)
{
    core::ArtifactIndexEntry entry;
    entry.artifact_id       = header.artifact_id;
    entry.type              = static_cast<core::ArtifactType>(header.type);
    entry.timestamp_us      = header.timestamp_us;
    entry.file_offset       = offset;
    entry.subrecord_offset  = 0;
    entry.byte_length       = static_cast<uint32_t>(sizeof(CacheRecordHeader)) + header.payload_size;
    entry.lat               = header.lat;
    entry.lon               = header.lon;
    entry.ping_number       = header.ping_number;
    entry.frequency_hz      = header.frequency_hz;
    entry.spatial_ref_kind  = spatialRefKindForRecord(file_ref, header.is_projected != 0);
    entry.is_projected      = header.is_projected != 0;

    // Defensive: if stored as geographic but lat/lon are outside WGS-84 bounds
    // the original classification was wrong (e.g. SEG-Y with a bad units field).
    // Correct it here so old caches heal on next read without a reindex.
    if (!entry.is_projected
            && (std::fabs(entry.lon) > 180.0 || std::fabs(entry.lat) > 90.0)) {
        entry.is_projected    = true;
        entry.spatial_ref_kind = core::SpatialRefKind::Projected;
    }

    index.entries.push_back(entry);
}

static inline std::string tempPathFor(const std::string& cache_path)
{
    return cache_path + ".tmp";
}

static inline void removeIfExists(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path), ec);
}

// -- Index footer helpers ------------------------------------------------------

// Write the compact index footer after all records have been written.
// Appends [N × CacheIndexEntry][CacheIndexFooter] to the current file position.
static inline bool writeIndexFooter(FILE* file,
                                    const core::ArtifactIndex& index,
                                    uint32_t correction_flags_seen,
                                    uint8_t  bottom_pick_src_mask)
{
    for (const auto& e : index.entries) {
        CacheIndexEntry ce{};
        ce.artifact_id      = e.artifact_id;
        ce.timestamp_us     = e.timestamp_us;
        ce.file_offset      = e.file_offset;
        ce.byte_length      = e.byte_length;
        ce.frequency_hz     = e.frequency_hz;
        ce.ping_number      = e.ping_number;
        ce.lat              = e.lat;
        ce.lon              = e.lon;
        ce.type             = static_cast<uint8_t>(e.type);
        ce.is_projected     = e.is_projected ? 1u : 0u;
        ce.spatial_ref_kind = static_cast<uint8_t>(e.spatial_ref_kind);
        if (!writePod(file, ce)) return false;
    }
    CacheIndexFooter footer{};
    footer.entry_count          = static_cast<uint32_t>(index.entries.size());
    footer.correction_flags_seen = correction_flags_seen;
    footer.bottom_pick_src_mask  = bottom_pick_src_mask;
    footer.footer_version        = kIndexFooterVersion;
    std::memcpy(footer.magic, kIndexFooterMagic.data(), kIndexFooterMagic.size());
    return writePod(file, footer);
}

// Try to load the compact index from the footer section.
// Returns true on success; caller must still check index.empty() for validity.
// On success also populates correction_flags_seen and bottom_pick_src_mask via meta.
static inline bool tryReadIndexFooter(FILE* file,
                                      uint64_t file_size,
                                      const core::SpatialRef& file_ref,
                                      core::ArtifactIndex& index,
                                      FormatMeta& meta)
{
    static constexpr uint64_t kFooterSz = sizeof(CacheIndexFooter);
    static constexpr uint64_t kEntrySz  = sizeof(CacheIndexEntry);

    if (file_size < sizeof(CacheFileHeader) + kFooterSz) return false;

    // Read footer from the end of the file.
    if (!seekFile(file, file_size - kFooterSz)) return false;
    CacheIndexFooter footer{};
    if (!readPod(file, footer)) return false;
    if (!sameMagic(footer.magic, kIndexFooterMagic)) return false;
    if (footer.footer_version != kIndexFooterVersion) return false;

    // Validate that the entries block fits between the file header and footer.
    const uint64_t entries_block = static_cast<uint64_t>(footer.entry_count) * kEntrySz;
    const uint64_t entries_start = file_size - kFooterSz - entries_block;
    if (entries_start < sizeof(CacheFileHeader)) return false;
    if (entries_start + entries_block + kFooterSz != file_size) return false;

    if (!seekFile(file, entries_start)) return false;

    index.entries.reserve(footer.entry_count);
    for (uint32_t i = 0; i < footer.entry_count; ++i) {
        CacheIndexEntry ce{};
        if (!readPod(file, ce)) { index.entries.clear(); return false; }
        core::ArtifactIndexEntry e{};
        e.artifact_id      = ce.artifact_id;
        e.timestamp_us     = ce.timestamp_us;
        e.file_offset      = ce.file_offset;
        e.subrecord_offset = 0;
        e.byte_length      = ce.byte_length;
        e.frequency_hz     = ce.frequency_hz;
        e.ping_number      = ce.ping_number;
        e.lat              = ce.lat;
        e.lon              = ce.lon;
        e.type             = static_cast<core::ArtifactType>(ce.type);
        e.is_projected     = ce.is_projected != 0;
        e.spatial_ref_kind = static_cast<core::SpatialRefKind>(ce.spatial_ref_kind);

        // Apply the same geographic-bounds fix as appendIndexEntry.
        if (!e.is_projected && (std::fabs(e.lon) > 180.0 || std::fabs(e.lat) > 90.0)) {
            e.is_projected    = true;
            e.spatial_ref_kind = core::SpatialRefKind::Projected;
        }
        (void)file_ref;  // coordinate_ref is baked per-entry into spatial_ref_kind

        index.entries.push_back(e);
    }

    meta.correction_flags_seen = footer.correction_flags_seen;
    meta.bottom_pick_src_mask  = footer.bottom_pick_src_mask;
    return true;
}

} // namespace dolphin::io::detail_cache
