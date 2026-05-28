#pragma once
// JsfReader_p.h — binary structs and helpers shared across JsfReader TUs.
#include "io/jsf/JsfReader.h"
#include "io/FileIo.h"
#include <cmath>

namespace dolphin::io::detail_jsf {

#pragma pack(push, 1)

struct JsfPacketHeader {
    uint16_t marker;
    uint8_t  version;
    uint8_t  session_id;
    uint16_t type;
    uint8_t  cmd_type;
    uint8_t  subsystem;
    uint8_t  channel;
    uint8_t  seq_num;
    uint16_t reserved;
    uint32_t size;
};

struct JsfSonarPingHeader {
    uint32_t time_sec;
    uint32_t time_usec;
    float    ship_speed_kn;
    double   ship_lat;
    double   ship_lon;
    float    course_deg;
    float    pitch_deg;
    float    roll_deg;
    float    heave_m;
    float    heading_deg;
    uint32_t num_samples;
    uint16_t sample_size_bits;
    int16_t  reserved1;
    float    range_m;
    uint32_t frequency_hz;
    uint32_t weight_factor;
    uint16_t pulse_type;
    int16_t  reserved2;
    float    scanning_freq;
    float    gain_factor;
    uint16_t user_gain;
    int16_t  reserved3;
    float    time_offset_sec;
    float    altitude_m;
    float    sensor_depth_m;
    float    sensor_lat;
    float    sensor_lon;
    float    sensor_speed_kn;
    float    cable_out_m;
    float    layback_m;
    uint8_t  reserved4[124];
};
static_assert(sizeof(JsfSonarPingHeader) == 240,
              "JsfSonarPingHeader must be exactly 240 bytes");

#pragma pack(pop)

static inline int64_t pingTimestampUs(const JsfSonarPingHeader& h)
{
    return static_cast<int64_t>(h.time_sec) * 1'000'000LL
         + static_cast<int64_t>(h.time_usec);
}

static inline uint32_t bytesPerSample(uint16_t sample_size_bits)
{
    return (sample_size_bits <= 8) ? 1u : 2u;
}

static inline bool isFiniteCoordinate(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon);
}

// Returns true when float sensor coords look like a real fix, not an unset/garbage field.
static inline bool hasUsableSensorCoord(float lat_f, float lon_f)
{
    return std::isfinite(lat_f) && std::isfinite(lon_f)
        && (lat_f != 0.f || lon_f != 0.f);
}

// Returns true when double coords are finite and not null-island.
static inline bool hasUsableCoordinate(double lat, double lon)
{
    return isFiniteCoordinate(lat, lon) && (lat != 0.0 || lon != 0.0);
}

} // namespace dolphin::io::detail_jsf
