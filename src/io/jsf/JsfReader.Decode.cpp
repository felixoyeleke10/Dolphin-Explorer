// JsfReader.Decode.cpp — JsfReader::readArtifact.
#include "io/jsf/JsfReader_p.h"
#include "io/detail/SubBottomDetect.h"
#include <algorithm>

namespace dolphin::io {

using namespace detail_jsf;

std::optional<core::Artifact>
JsfReader::readArtifact(const core::ArtifactIndexEntry& entry)
{
    if (!m_file) return std::nullopt;

    if (!detail::seekAbs(m_file, entry.file_offset))
        return std::nullopt;

    JsfPacketHeader pkt{};
    if (fread(&pkt, sizeof(pkt), 1, m_file) != 1) return std::nullopt;
    if (pkt.marker != JSF_MARKER)  return std::nullopt;
    if (pkt.type   != MSG_SONAR)   return std::nullopt;
    if (pkt.size   <  kPingHdrSize) return std::nullopt;

    JsfSonarPingHeader ph{};
    if (fread(&ph, sizeof(ph), 1, m_file) != 1) return std::nullopt;

    int64_t ts_us = pingTimestampUs(ph);

    // Preserve both raw position sources so the user can choose fish vs vessel nav.
    const bool sensor_ok = hasUsableSensorCoord(ph.sensor_lat, ph.sensor_lon);
    const bool ship_ok   = hasUsableCoordinate(ph.ship_lat, ph.ship_lon);

    // Resolved best-available position (existing fallback order unchanged).
    double lat = sensor_ok ? static_cast<double>(ph.sensor_lat) : ph.ship_lat;
    double lon = sensor_ok ? static_cast<double>(ph.sensor_lon) : ph.ship_lon;

    core::NavPoint nav;
    nav.lat          = lat;
    nav.lon          = lon;
    nav.fish_lat         = sensor_ok ? static_cast<double>(ph.sensor_lat) : 0.0;
    nav.fish_lon         = sensor_ok ? static_cast<double>(ph.sensor_lon) : 0.0;
    nav.fish_nav_valid   = sensor_ok;
    nav.vessel_lat       = ship_ok ? ph.ship_lat : 0.0;
    nav.vessel_lon       = ship_ok ? ph.ship_lon : 0.0;
    nav.vessel_nav_valid = ship_ok;
    nav.heading_deg        = ph.heading_deg;
    nav.sensor_heading_deg = ph.heading_deg;  // JSF has one heading field (towfish)
    // course_deg is a separate GPS-derived COG; leave ship_heading_deg at 0
    nav.speed_kn     = ph.sensor_speed_kn;
    nav.altitude_m   = ph.altitude_m;
    nav.pitch_deg    = ph.pitch_deg;
    nav.roll_deg     = ph.roll_deg;
    nav.heave_m      = ph.heave_m;
    nav.timestamp    = ts_us * 1e-6;
    nav.valid        = hasUsableCoordinate(lat, lon);
    nav.is_projected = entry.is_projected;
    nav.spatial_ref  = entry.is_projected
        ? core::makeUnknownProjectedSpatialRef("PROJECTED:JSF_MAGNITUDE")
        : core::makeWgs84SpatialRef();

    uint32_t num_samples = ph.num_samples;
    if (num_samples == 0) return std::nullopt;

    uint32_t bps         = bytesPerSample(ph.sample_size_bits);
    uint32_t body_remain = pkt.size - kPingHdrSize;
    uint32_t max_samples = (bps > 0) ? (body_remain / bps) : 0;
    num_samples = std::min(num_samples, max_samples);
    if (num_samples == 0) return std::nullopt;

    // ── Sub-bottom trace ──────────────────────────────────────────────────────
    if (entry.type == core::ArtifactType::SubBottom) {
        core::SubBottomTrace trace;
        trace.id             = entry.artifact_id;
        trace.timestamp_us   = ts_us;
        trace.nav            = nav;
        trace.frequency_hz   = static_cast<float>(ph.frequency_hz);
        trace.tow_depth_m    = ph.sensor_depth_m;
        trace.two_way_time_s = (ph.range_m > 0.f) ? (2.f * ph.range_m / 1500.f) : 0.f;
        trace.sample_rate_hz = (trace.two_way_time_s > 0.f)
                             ? static_cast<float>(num_samples) / trace.two_way_time_s
                             : 0.f;
        trace.samples.reserve(num_samples);
        for (uint32_t s = 0; s < num_samples; ++s) {
            float normalised = 0.f;
            if (bps == 1) {
                uint8_t raw = 0;
                if (fread(&raw, 1, 1, m_file) != 1) break;
                normalised = (static_cast<float>(raw) - 128.f) / 128.f;
            } else {
                int16_t raw = 0;
                if (fread(&raw, 2, 1, m_file) != 1) break;
                normalised = static_cast<float>(raw) / 32768.f;
            }
            trace.samples.push_back(normalised);
        }
        trace.bottom_sample_idx = detectBottomSample(
            trace.samples.data(), static_cast<int>(trace.samples.size()));
        return trace;
    }

    // ── Sidescan ping ─────────────────────────────────────────────────────────
    static constexpr float kDefaultSV = 1500.f;  // JSF has no SV field; use standard assumption

    core::SidescanPing ping;
    ping.id                = entry.artifact_id;
    ping.timestamp_us      = ts_us;
    ping.nav               = nav;
    ping.frequency_hz      = static_cast<float>(ph.frequency_hz);
    ping.slant_range_m     = ph.range_m;
    ping.sound_velocity_ms = kDefaultSV;
    ping.tow_depth_m       = ph.sensor_depth_m;
    ping.blanking_m        = (ph.time_offset_sec > 0.f)
                           ? (ph.time_offset_sec * kDefaultSV * 0.5f)
                           : 0.f;
    ping.layback_m         = ph.layback_m;
    ping.cable_out_m       = ph.cable_out_m;
    ping.gain_code         = static_cast<uint16_t>(ph.weight_factor & 0xFFFFu);
    ping.sample_rate_hz    = (ph.range_m > 0.f && num_samples > 0)
                           ? (static_cast<float>(num_samples) * kDefaultSV / (2.f * ph.range_m))
                           : 0.f;
    ping.channel = (pkt.subsystem == SUBSYS_STBD)
                 ? core::SidescanChannel::Starboard
                 : core::SidescanChannel::Port;

    ping.samples.reserve(num_samples);
    for (uint32_t s = 0; s < num_samples; ++s) {
        uint16_t raw = 0;
        if (bps == 1) {
            uint8_t b = 0;
            if (fread(&b, 1, 1, m_file) != 1) break;
            raw = static_cast<uint16_t>(b) * 257u;
        } else {
            if (fread(&raw, 2, 1, m_file) != 1) break;
        }
        core::SidescanSample sample;
        sample.amplitude = raw;
        sample.range_m   = (num_samples > 1)
                         ? ping.blanking_m + (ping.slant_range_m - ping.blanking_m)
                               * static_cast<float>(s) / static_cast<float>(num_samples - 1)
                         : ping.blanking_m;
        ping.samples.push_back(sample);
    }

    return ping;
}

} // namespace dolphin::io
