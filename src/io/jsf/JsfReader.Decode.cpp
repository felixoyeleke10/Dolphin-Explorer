// JsfReader.Decode.cpp — EdgeTech Message Type 80 artifact decoding.
#include "io/jsf/JsfReader_p.h"
#include "io/detail/SubBottomDetect.h"

#include <algorithm>
#include <cmath>

namespace dolphin::io {

using namespace detail_jsf;

namespace {

bool readWord(FILE* file, uint16_t& value)
{
    uint8_t bytes[2]{};
    if (std::fread(bytes, sizeof(bytes), 1, file) != 1) return false;
    value = leU16(bytes);
    return true;
}

float sbpSample(FILE* file, uint16_t format, bool& ok)
{
    uint16_t first = 0;
    if (!readWord(file, first)) { ok = false; return 0.0f; }

    if (format == 1 || format == 9) {
        uint16_t second = 0;
        if (!readWord(file, second)) { ok = false; return 0.0f; }
        const float real = static_cast<float>(static_cast<int16_t>(first));
        const float imag = static_cast<float>(static_cast<int16_t>(second));
        return std::min(1.0f, std::hypot(real, imag) / 46340.95f);
    }
    if (format == 2 || format == 3)
        return static_cast<float>(static_cast<int16_t>(first)) / 32768.0f;
    return static_cast<float>(first) / 65535.0f; // envelope / pixel data
}

uint16_t sidescanSample(FILE* file, uint16_t format, bool& ok)
{
    uint16_t first = 0;
    if (!readWord(file, first)) { ok = false; return 0; }

    if (format == 1 || format == 9) {
        uint16_t second = 0;
        if (!readWord(file, second)) { ok = false; return 0; }
        const double real = static_cast<double>(static_cast<int16_t>(first));
        const double imag = static_cast<double>(static_cast<int16_t>(second));
        return static_cast<uint16_t>(std::min(65535.0, std::hypot(real, imag)));
    }
    if (format == 2 || format == 3) {
        const int sample = static_cast<int>(static_cast<int16_t>(first));
        return static_cast<uint16_t>(
            std::min(65535, std::abs(sample) * 2));
    }
    return first; // envelope / pixel data
}

} // namespace

std::optional<core::Artifact>
JsfReader::readArtifact(const core::ArtifactIndexEntry& entry)
{
    if (!m_file || !detail::seekAbs(m_file, entry.file_offset))
        return std::nullopt;

    JsfPacketHeader pkt{};
    if (std::fread(&pkt, sizeof(pkt), 1, m_file) != 1
            || pkt.marker != JSF_MARKER
            || pkt.type != MSG_SONAR
            || !supportsSubsystem(pkt.subsystem)
            || pkt.size < kPingHdrSize
            || pkt.size > kMaxRecordSz
            || (entry.byte_length != 0
                && static_cast<uint64_t>(sizeof(JsfPacketHeader)) + pkt.size
                    > entry.byte_length)) {
        return std::nullopt;
    }

    JsfSonarPingHeader ph{};
    if (std::fread(&ph, sizeof(ph), 1, m_file) != 1) return std::nullopt;

    JsfSampleLayout layout;
    if (!sampleLayout(ph, pkt.version, pkt.size, layout)) return std::nullopt;

    const int64_t ts_us = pingTimestampUs(ph);
    const JsfCoordinate pos = coordinate(ph);
    const uint16_t flags = validityFlags(ph);

    core::NavPoint nav;
    nav.lat          = pos.lat;
    nav.lon          = pos.lon;
    nav.heading_deg  = headingDeg(ph);
    nav.sensor_heading_deg = nav.heading_deg;
    nav.altitude_m   = altitudeM(ph);
    nav.pitch_deg    = pitchDeg(ph);
    nav.roll_deg     = rollDeg(ph);
    nav.timestamp    = ts_us * 1e-6;
    nav.valid        = pos.valid;
    nav.is_projected = pos.projected;
    nav.spatial_ref  = pos.projected
        ? core::makeUnknownProjectedSpatialRef("PROJECTED:JSF")
        : core::makeWgs84SpatialRef();

    // Position-interpolated means the trace coordinate represents the sonar;
    // otherwise the format documents it as the last received navigation fix.
    if (pos.valid && (flags & (1u << 13))) {
        nav.fish_lat       = pos.lat;
        nav.fish_lon       = pos.lon;
        nav.fish_nav_valid = true;
    } else if (pos.valid) {
        nav.vessel_lat       = pos.lat;
        nav.vessel_lon       = pos.lon;
        nav.vessel_nav_valid = true;
    }

    const uint32_t interval_ns = sampleIntervalNs(ph);
    const float sample_rate_hz = interval_ns > 0
        ? 1.0e9f / static_cast<float>(interval_ns) : 0.0f;
    const float range_m = interval_ns > 0
        ? static_cast<float>(layout.count) * static_cast<float>(interval_ns)
            * 1.0e-9f * 1500.0f * 0.5f
        : 0.0f;
    const float blanking_m = interval_ns > 0
        ? static_cast<float>(leU32(&ph.bytes[4])) * static_cast<float>(interval_ns)
            * 1.0e-9f * 1500.0f * 0.5f
        : 0.0f;
    const float frequency_hz = static_cast<float>(frequencyHz(ph, pkt.version));
    const auto artifact_type = classifySubsystem(pkt.subsystem);

    if (artifact_type == core::ArtifactType::SubBottom) {
        core::SubBottomTrace trace;
        trace.id             = entry.artifact_id;
        trace.timestamp_us   = ts_us;
        trace.nav            = nav;
        trace.frequency_hz   = frequency_hz;
        trace.sample_rate_hz = sample_rate_hz;
        trace.tow_depth_m    = depthM(ph);
        trace.two_way_time_s = interval_ns > 0
            ? static_cast<float>(layout.count) * interval_ns * 1.0e-9f : 0.0f;
        trace.samples.reserve(layout.count);
        bool ok = true;
        for (uint32_t i = 0; i < layout.count; ++i)
            trace.samples.push_back(sbpSample(m_file, layout.format, ok));
        if (!ok) return std::nullopt;
        trace.bottom_sample_idx = detectBottomSample(
            trace.samples.data(), static_cast<int>(trace.samples.size()));
        return trace;
    }

    core::SidescanPing ping;
    ping.id                = entry.artifact_id;
    ping.timestamp_us      = ts_us;
    ping.ping_number       = pingNumber(ph);
    ping.nav               = nav;
    ping.frequency_hz      = frequency_hz;
    ping.sample_rate_hz    = sample_rate_hz;
    ping.slant_range_m     = range_m;
    ping.sound_velocity_ms = 1500.0f; // JSF Message 80 has no per-trace SV field
    ping.tow_depth_m       = depthM(ph);
    ping.blanking_m        = blanking_m;
    ping.layback_m         = laybackM(ph);
    ping.cable_out_m       = cableOutM(ph);
    ping.gain_code         = leU16(&ph.bytes[120]);
    ping.channel = pkt.channel == CHANNEL_STBD
        ? core::SidescanChannel::Starboard
        : core::SidescanChannel::Port;

    ping.samples.reserve(layout.count);
    bool ok = true;
    for (uint32_t i = 0; i < layout.count; ++i) {
        core::SidescanSample sample;
        sample.amplitude = sidescanSample(m_file, layout.format, ok);
        sample.range_m = layout.count > 1
            ? blanking_m + (range_m - blanking_m)
                * static_cast<float>(i) / static_cast<float>(layout.count - 1)
            : blanking_m;
        ping.samples.push_back(sample);
    }
    if (!ok) return std::nullopt;
    return ping;
}

} // namespace dolphin::io
