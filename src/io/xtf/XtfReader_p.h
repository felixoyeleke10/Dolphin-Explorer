// XtfReader_p.h — private XTF binary structures and shared helpers.
// Included by XtfReader.cpp, XtfIndex.cpp, and XtfPayload.cpp only.
#pragma once
#include "core/Artifact.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace dolphin::io {

// -- XTF on-disk binary structures (packed, little-endian) --------------------

#pragma pack(push, 1)

struct XtfFileHeader {
    uint8_t  FileFormat;        // 0x7B = 123
    uint8_t  SystemType;
    char     RecordingProgramName[8];
    char     RecordingProgramVersion[8];
    char     SonarName[16];
    uint16_t SonarType;
    char     NoteString[64];
    char     ThisFileName[64];
    uint16_t NavUnits;
    uint16_t NumberOfSonarChannels;
    uint16_t NumberOfBathymetryChannels;
    uint8_t  Reserved[86];      // pad to 256 bytes total (XTF spec §2.1)
    // XtfChanInfo blocks start at offset 256, up to offset 1024
};
static_assert(sizeof(XtfFileHeader) == 256, "XtfFileHeader must be 256 bytes");

struct XtfChanInfo {
    uint8_t  TypeOfChannel;      // 0=subbottom, 1=port SSS, 2=stbd SSS, 3/4=bathy
    uint8_t  SubChannelNumber;   // sub-channel within this channel type
    uint16_t CorrectionFlags;
    uint16_t UniPolar;
    uint16_t BytesPerSample;     // 1 = 8-bit samples, 2 = 16-bit samples
    uint32_t SamplesPerChannel;
    char     ChannelName[16];
    float    VoltScale;
    float    Frequency;
    float    HorizBeamAngle;
    float    TiltAngle;
    float    BeamWidth;
    float    OffsetX, OffsetY, OffsetZ;
    float    OffsetYaw, OffsetPitch, OffsetRoll;
    uint16_t BeamsPerArray;
    uint8_t  Reserved[54];      // pad to 128 bytes total (XTF spec §2.2)
};
static_assert(sizeof(XtfChanInfo) == 128, "XtfChanInfo must be 128 bytes");

struct XtfPacketHeader {
    uint16_t MagicNumber;   // 0xFACE
    uint8_t  HeaderType;
    uint8_t  SubChannelNumber;
    uint16_t NumChansToFollow;
    uint16_t Reserved1[2];
    uint32_t NumBytesThisRecord;
    uint16_t Year;
    uint8_t  Month, Day, Hour, Minute, Second;
    uint8_t  HSeconds;
    uint16_t JulianDay;
    uint32_t EventNumber;
    uint32_t PingNumber;
    float    SoundVelocity;
    float    OceanTide;
    uint32_t Reserved2;
    float    ConductivityFreq;
    float    TemperatureFreq;
    float    PressureFreq;
    float    PressureTemp;
    float    Conductivity;
    float    WaterTemperature;
    float    Pressure;
    float    ComputedSoundVelocity;
    float    MagX, MagY, MagZ;
    float    AuxVal1, AuxVal2, AuxVal3, AuxVal4, AuxVal5, AuxVal6;
    float    SpeedLog;
    float    Turbidity;
    float    ShipSpeed;
    float    ShipGyro;
    double   ShipYcoordinate;  // latitude
    double   ShipXcoordinate;  // longitude
    uint16_t ShipAltitude;
    uint16_t ShipDepth;
    uint8_t  FixTimeHour, FixTimeMinute, FixTimeSecond;
    uint8_t  FixTimeHSeconds;
    float    SensorSpeed;
    float    KP;
    double   SensorYcoordinate;  // sensor latitude
    double   SensorXcoordinate;  // sensor longitude
    uint16_t SonarStatus;
    uint16_t RangeToFish;
    uint16_t BearingToFish;
    uint16_t CableOut;
    float    Layback;
    float    CableTension;
    float    SensorDepth;
    float    SensorPrimaryAltitude;
    float    SensorAuxAltitude;
    float    SensorPitch;
    float    SensorRoll;
    float    SensorHeading;
    float    Heave;
    float    Yaw;
    uint32_t AttitudeTimeTag;
    float    DOT;
    uint32_t NavFixMilliseconds;
    uint8_t  ComputerClockHour, ComputerClockMinute,
             ComputerClockSecond, ComputerClockHsec;
    int16_t  FishPositionDeltaX, FishPositionDeltaY;
    uint8_t  FishPositionErrorCode;
    uint32_t OptionalOffset;
    uint8_t  CableOutHundredths;
    uint8_t  ReservedSpace2[6];
};
static_assert(sizeof(XtfPacketHeader) == 256, "XtfPacketHeader must be 256 bytes");

struct XtfPingChanHeader {
    uint16_t ChannelNumber;
    uint16_t DownsampleMethod;
    float    SlantRange;
    float    GroundRange;
    float    TimeDelay;
    float    TimeDuration;
    float    SecondsPerPing;
    uint16_t ProcessingFlags;
    uint16_t Frequency;
    uint16_t InitialGainCode;
    uint16_t GainCode;
    uint16_t BandWidth;
    uint32_t ContactNumber;
    uint16_t ContactClassification;
    uint8_t  ContactSubNumber;
    uint8_t  ContactType;
    uint32_t NumSamples;
    uint16_t MillivoltScale;
    float    ContactTimeOffTrack;
    uint8_t  ContactCloseNumber;
    uint8_t  Reserved;
    float    FixedVSOP;
    int16_t  Weight;
    uint8_t  ReservedSpace[4];
};
static_assert(sizeof(XtfPingChanHeader) == 64, "XtfPingChanHeader must be 64 bytes");

#pragma pack(pop)

// -- Channel type constants ----------------------------------------------------

static constexpr uint16_t XTF_MAGIC     = 0xFACE;
static constexpr uint16_t CHAN_SUBBOT    = 0;
static constexpr uint16_t CHAN_PORT_SSS  = 1;
static constexpr uint16_t CHAN_STBD_SSS  = 2;
// Recognized but not yet supported: bathymetry channels (some systems use 4).
static constexpr uint16_t CHAN_BATHY     = 3;
static constexpr uint16_t CHAN_BATHY_ALT = 4;

static constexpr uint8_t  PACKET_PING   = 0;
static constexpr uint8_t  PACKET_NAV    = 42;

// -- Coordinate helpers --------------------------------------------------------

static inline bool isFiniteCoordinate(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon);
}

static inline bool hasUsableCoordinate(double lat, double lon)
{
    return isFiniteCoordinate(lat, lon) && (lat != 0.0 || lon != 0.0);
}

static inline core::SpatialRef coordinateRefFromNavUnits(uint16_t nav_units)
{
    if (nav_units == 3)
        return core::makeUnknownProjectedSpatialRef("PROJECTED:XTF_NAVUNITS3");
    if (nav_units == 0 || nav_units == 1)
        return core::makeWgs84SpatialRef();
    return {};
}

static inline core::SpatialRef coordinateRefFromMagnitude(double lat, double lon)
{
    if (!isFiniteCoordinate(lat, lon))
        return {};
    if (std::abs(lat) > 90.0 || std::abs(lon) > 180.0)
        return core::makeUnknownProjectedSpatialRef("PROJECTED:XTF_MAGNITUDE");
    return core::makeWgs84SpatialRef();
}

static inline core::SpatialRef coordinateRefFromFlags(bool coords_projected, bool known)
{
    if (!known)
        return {};
    return coords_projected
        ? core::makeUnknownProjectedSpatialRef()
        : core::makeWgs84SpatialRef();
}

static inline std::string lowerAscii(const std::string& text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

// Convert XTF packet timestamp to microseconds since Unix epoch.
// Uses Year + JulianDay (1-based) with proper leap-year accounting.
static inline int64_t packetTimestampUs(const XtfPacketHeader& p)
{
    auto is_leap = [](int y) -> bool {
        return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
    };

    int year = static_cast<int>(p.Year);
    if (year < 1970) year = 1970;

    int64_t days = 0;
    for (int y = 1970; y < year; ++y)
        days += is_leap(y) ? 366 : 365;

    int jday = static_cast<int>(p.JulianDay);
    if (jday > 0) days += jday - 1;

    int64_t secs = days * 86400LL
                 + p.Hour   * 3600LL
                 + p.Minute * 60LL
                 + p.Second;
    return secs * 1'000'000LL + p.HSeconds * 10'000LL;
}

// Infer bps from whole-record geometry.
// record_bytes = pkt_hdr + n_chans*(chan_hdr + n_samples*bps)
// Returns 0 if inference is not possible.
static inline uint16_t inferBpsFromRecord(uint32_t record_bytes,
                                          uint16_t num_chans,
                                          uint32_t samples_per_chan)
{
    if (num_chans == 0 || samples_per_chan == 0) return 0;
    const uint64_t hdr_total = sizeof(XtfPacketHeader)
        + static_cast<uint64_t>(num_chans) * sizeof(XtfPingChanHeader);
    if (static_cast<uint64_t>(record_bytes) <= hdr_total) return 0;
    const uint64_t data_bytes    = record_bytes - hdr_total;
    const uint64_t total_samples = static_cast<uint64_t>(num_chans) * samples_per_chan;
    const uint64_t bpp = data_bytes / total_samples;
    if (bpp >= 4) return 4;
    if (bpp >= 2) return 2;
    return 1;
}

} // namespace dolphin::io
