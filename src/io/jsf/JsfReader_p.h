#pragma once
// JsfReader_p.h — binary structs and helpers shared across JsfReader TUs.
#include "io/jsf/JsfReader.h"
#include "io/FileIo.h"
#include <bit>
#include <cmath>
#include <string>

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
static_assert(sizeof(JsfPacketHeader) == 16,
              "JsfPacketHeader must be exactly 16 bytes");

struct JsfSonarPingHeader {
    uint8_t bytes[240];
};
static_assert(sizeof(JsfSonarPingHeader) == 240,
              "JsfSonarPingHeader must be exactly 240 bytes");

#pragma pack(pop)

static inline uint16_t leU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0])
         | static_cast<uint16_t>(p[1]) << 8;
}

static inline int16_t leI16(const uint8_t* p)
{
    return static_cast<int16_t>(leU16(p));
}

static inline uint32_t leU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
         | static_cast<uint32_t>(p[1]) << 8
         | static_cast<uint32_t>(p[2]) << 16
         | static_cast<uint32_t>(p[3]) << 24;
}

static inline int32_t leI32(const uint8_t* p)
{
    return static_cast<int32_t>(leU32(p));
}

static inline float leF32(const uint8_t* p)
{
    return std::bit_cast<float>(leU32(p));
}

static inline uint32_t pingNumber(const JsfSonarPingHeader& h)
{
    return leU32(&h.bytes[8]);
}

static inline uint16_t validityFlags(const JsfSonarPingHeader& h)
{
    return leU16(&h.bytes[30]);
}

static inline uint16_t dataFormat(const JsfSonarPingHeader& h)
{
    return leU16(&h.bytes[34]);
}

static inline uint32_t sampleCount(const JsfSonarPingHeader& h,
                                   uint8_t protocol_version)
{
    uint32_t count = leU16(&h.bytes[114]);
    if (protocol_version >= 0x0A) {
        const uint16_t msb = leU16(&h.bytes[16]);
        count |= static_cast<uint32_t>((msb >> 8) & 0x0F) << 16;
    }
    return count;
}

static inline uint32_t sampleIntervalNs(const JsfSonarPingHeader& h)
{
    return leU32(&h.bytes[116]);
}

static inline uint32_t frequencyHz(const JsfSonarPingHeader& h,
                                   uint8_t protocol_version)
{
    uint32_t start = leU16(&h.bytes[126]);
    uint32_t end   = leU16(&h.bytes[128]);
    if (protocol_version >= 0x0A) {
        const uint16_t msb = leU16(&h.bytes[16]);
        start |= static_cast<uint32_t>(msb & 0x0F) << 16;
        end   |= static_cast<uint32_t>((msb >> 4) & 0x0F) << 16;
    }
    if (start == 0) return end * 10u;
    if (end == 0) return start * 10u;
    return ((start + end) / 2u) * 10u;
}

static inline int64_t pingTimestampUs(const JsfSonarPingHeader& h)
{
    const uint32_t seconds = leU32(&h.bytes[0]);
    if (seconds != 0) {
        const uint32_t milliseconds_today = leU32(&h.bytes[200]);
        return static_cast<int64_t>(seconds) * 1'000'000LL
             + static_cast<int64_t>(milliseconds_today % 1000u) * 1000LL;
    }

    // Protocols before revision 8 leave TimeSince1970 at zero. The legacy
    // SEG-Y-style date fields at bytes 156..165 remain available.
    const int year = leU16(&h.bytes[156]);
    const int doy  = leU16(&h.bytes[158]);
    const int hour = leU16(&h.bytes[160]);
    const int min  = leU16(&h.bytes[162]);
    const int sec  = leU16(&h.bytes[164]);
    const auto is_leap = [](int y) {
        return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    };
    if (year < 1970 || year > 2100 || doy < 1
            || doy > (is_leap(year) ? 366 : 365)
            || hour < 0 || hour >= 24 || min < 0 || min >= 60
            || sec < 0 || sec >= 60) {
        return 0;
    }
    int64_t days = 0;
    for (int y = 1970; y < year; ++y) days += is_leap(y) ? 366 : 365;
    days += doy - 1;
    return (days * 86400LL + hour * 3600LL + min * 60LL + sec) * 1'000'000LL;
}

struct JsfCoordinate {
    double lon = 0.0;
    double lat = 0.0;
    bool valid = false;
    bool projected = false;
};

static inline JsfCoordinate coordinate(const JsfSonarPingHeader& h)
{
    JsfCoordinate out;
    if ((validityFlags(h) & 0x0001u) == 0) return out;

    const int32_t raw_x = leI32(&h.bytes[80]);
    const int32_t raw_y = leI32(&h.bytes[84]);
    if (raw_x == 0 && raw_y == 0) return out;

    switch (leU16(&h.bytes[88])) {
    case 1: // millimetres
        out.lon = static_cast<double>(raw_x) / 1000.0;
        out.lat = static_cast<double>(raw_y) / 1000.0;
        out.projected = true;
        break;
    case 2: // 1/10000 arc-minute
        out.lon = static_cast<double>(raw_x) / 600000.0;
        out.lat = static_cast<double>(raw_y) / 600000.0;
        if (std::abs(out.lon) > 180.0 || std::abs(out.lat) > 90.0) return {};
        break;
    case 3: // decimetres
        out.lon = static_cast<double>(raw_x) / 10.0;
        out.lat = static_cast<double>(raw_y) / 10.0;
        out.projected = true;
        break;
    case 4: // centimetres (protocol 0x11+)
        out.lon = static_cast<double>(raw_x) / 100.0;
        out.lat = static_cast<double>(raw_y) / 100.0;
        out.projected = true;
        break;
    default:
        return out;
    }
    out.valid = std::isfinite(out.lon) && std::isfinite(out.lat);
    return out;
}

static inline float headingDeg(const JsfSonarPingHeader& h)
{
    return (validityFlags(h) & (1u << 3))
        ? static_cast<float>(leU16(&h.bytes[172])) / 100.0f : 0.0f;
}

static inline float pitchDeg(const JsfSonarPingHeader& h)
{
    return (validityFlags(h) & (1u << 5))
        ? static_cast<float>(leI16(&h.bytes[174])) * (180.0f / 32768.0f) : 0.0f;
}

static inline float rollDeg(const JsfSonarPingHeader& h)
{
    return (validityFlags(h) & (1u << 5))
        ? static_cast<float>(leI16(&h.bytes[176])) * (180.0f / 32768.0f) : 0.0f;
}

static inline float depthM(const JsfSonarPingHeader& h)
{
    return (validityFlags(h) & (1u << 9))
        ? static_cast<float>(leI32(&h.bytes[136])) / 1000.0f : 0.0f;
}

static inline float altitudeM(const JsfSonarPingHeader& h)
{
    return (validityFlags(h) & (1u << 6))
        ? static_cast<float>(leI32(&h.bytes[144])) / 1000.0f : 0.0f;
}

static inline float laybackM(const JsfSonarPingHeader& h)
{
    const float value = leF32(&h.bytes[228]);
    return std::isfinite(value) ? value : 0.0f;
}

static inline float cableOutM(const JsfSonarPingHeader& h)
{
    return (validityFlags(h) & (1u << 11))
        ? static_cast<float>(leU16(&h.bytes[236])) / 10.0f : 0.0f;
}

static inline uint32_t componentsPerSample(uint16_t format)
{
    switch (format) {
    case 0: // envelope
    case 2: // raw, pre-match-filter
    case 3: // real analytic component
    case 4: // pixel data
        return 1;
    case 1: // complex analytic (real + imaginary)
    case 9: // complex analytic, pre-match-filter
        return 2;
    default:
        return 0;
    }
}

struct JsfSampleLayout {
    uint32_t count = 0;
    uint32_t components = 0;
    uint16_t format = 0;
    uint64_t byte_count = 0;
};

static inline bool sampleLayout(const JsfSonarPingHeader& h,
                                uint8_t protocol_version,
                                uint32_t packet_size,
                                JsfSampleLayout& out,
                                std::string* error = nullptr)
{
    out = {};
    if (packet_size < sizeof(JsfSonarPingHeader)) {
        if (error) *error = "sonar message is shorter than its 240-byte trace header";
        return false;
    }
    out.format = dataFormat(h);
    out.components = componentsPerSample(out.format);
    if (out.components == 0) {
        if (error) {
            *error = out.format > 255
                ? "compressed JSF samples are not supported"
                : "unsupported JSF data format " + std::to_string(out.format);
        }
        return false;
    }
    out.count = sampleCount(h, protocol_version);
    if (out.count == 0) {
        if (error) *error = "sonar message declares zero samples";
        return false;
    }
    out.byte_count = static_cast<uint64_t>(out.count) * out.components * 2u;
    const uint64_t available = packet_size - sizeof(JsfSonarPingHeader);
    if (out.byte_count > available) {
        if (error) *error = "declared JSF samples exceed the message boundary";
        return false;
    }
    return true;
}

} // namespace dolphin::io::detail_jsf
