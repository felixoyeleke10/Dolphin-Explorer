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
// Unsupported types (Multibeam, Raster) return payloadSize() == 0. Writers
// reject the whole candidate cache rather than publish a silently partial
// durable artifact store. Readers therefore never see these types in a DLPD.
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
#include <string>

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
    uint8_t  artifact_role = 0;  // kArtifactRole* (0 = original; was half of `reserved`)
    uint8_t  reserved = 0;
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

void setFileHeaderMetadata(CacheFileHeader& header, const FormatMeta& meta);
void loadFileHeaderMetadata(const CacheFileHeader& header, FormatMeta& meta);
core::SpatialRef spatialRefForRecord(const core::SpatialRef& file_ref, bool is_projected);
CacheNavHeader toCacheNav(const core::NavPoint& nav);
core::NavPoint fromCacheNav(const CacheNavHeader& nav);
void artifactLatLon(const core::Artifact& artifact, double& lat, double& lon);
uint32_t payloadSize(const core::Artifact& artifact);
bool writePayload(FILE* file, const core::Artifact& artifact);
bool readRecordHeader(FILE* file, CacheRecordHeader& header);
void appendIndexEntry(core::ArtifactIndex& index,
                      const CacheRecordHeader& header,
                      uint64_t offset,
                      const core::SpatialRef& file_ref);

void removeIfExists(const std::string& path);

// -- Index footer helpers ------------------------------------------------------

// Write the compact index footer after all records have been written.
// Appends [N × CacheIndexEntry][CacheIndexFooter] to the current file position.
bool writeIndexFooter(FILE* file,
                      const core::ArtifactIndex& index,
                      uint32_t correction_flags_seen,
                      uint8_t bottom_pick_src_mask);

// Try to load the compact index from the footer section.
// Returns true on success; caller must still check index.empty() for validity.
// On success also populates correction_flags_seen and bottom_pick_src_mask via meta.
bool tryReadIndexFooter(FILE* file,
                        uint64_t file_size,
                        const core::SpatialRef& file_ref,
                        core::ArtifactIndex& index,
                        FormatMeta& meta);

} // namespace dolphin::io::detail_cache
